// Cockpit mockup exporter.
//
// Loads the player ships, track01 (track, scenery, sky) and the HUD textures
// and font through the game's own loaders (object.c, image.c, track.c, ui.c),
// with a capture stub in place of the renderer: render_texture_create keeps
// the pixels, and the triangles that object_draw() pushes are recorded in the
// space of the model matrix that is set (identity for ships: model space).
//
// Everything written goes to <out_dir>, which must be under build/ (game data:
// never commit it). Usage: export_mockup <data_root> <out_dir>
// where <data_root> holds the lowercased PSX tree as wipeout/...

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../src/types.h"
#include "../../src/mem.h"
#include "../../src/utils.h"
#include "../../src/render.h"
#include "../../src/platform.h"
#include "../../src/wipeout/object.h"
#include "../../src/wipeout/image.h"
#include "../../src/wipeout/track.h"
#include "../../src/wipeout/game.h"
#include "../../src/wipeout/ui.h"

#include <stb_image_write.h>

// ---------------------------------------------------------------------------
// Globals the linked game files expect

game_t g;
save_t save;
const game_def_t def = {0}; // circuits[0].release == GAME_WIPEOUT: PSX track layout

int def_ship_model_to_pilot(int i);
double system_cycle_time(void) { return 0; }
double system_tick(void) { return 0; }
vec3_t camera_forward(camera_t *camera) { (void)camera; return vec3(0, 0, 1); }

// ---------------------------------------------------------------------------
// Platform stub: assets come from <data_root>/

static char data_root[1024];
static char path_buf[2048];

static const char *asset_path(const char *name) {
	snprintf(path_buf, sizeof(path_buf), "%s/%s", data_root, name);
	return path_buf;
}

void platform_exit(void) {}
vec2i_t platform_screen_size(void) { return vec2i(1920, 1080); }
double platform_now(void) { return 0; }
bool platform_get_fullscreen(void) { return false; }
void platform_set_fullscreen(bool fullscreen) { (void)fullscreen; }
void platform_set_audio_mix_cb(void (*cb)(float *buffer, uint32_t len)) { (void)cb; }
bool platform_asset_exists(const char *name) { return file_exists(asset_path(name)); }
FILE *platform_open_asset(const char *name, const char *mode) { return fopen(asset_path(name), mode); }
uint8_t *platform_load_asset(const char *name, uint32_t *bytes_read) { return file_load(asset_path(name), bytes_read); }
uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) { (void)name; *bytes_read = 0; return NULL; }
uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) { (void)name; (void)bytes; return len; }

// ---------------------------------------------------------------------------
// Capture renderer

typedef struct {
	uint32_t w, h;
	rgba_t *pixels;
} cap_texture_t;

#define CAP_TEXTURES_MAX 1024
static cap_texture_t textures[CAP_TEXTURES_MAX];
static uint16_t textures_len = 0;
uint16_t RENDER_NO_TEXTURE;

typedef struct {
	tris_t t;
	uint16_t texture;
	uint8_t double_sided;
} cap_tri_t;

static cap_tri_t *tris = NULL;
static uint32_t tris_len = 0, tris_cap = 0;
static mat4_t model_mat;

typedef struct {
	vec2i_t pos, uv_offset, uv_size, size;
	uint16_t texture;
} cap_tile_t;
static cap_tile_t last_tile;

void render_init(vec2i_t screen_size) {
	(void)screen_size;
	rgba_t white[4] = {rgba(128,128,128,255), rgba(128,128,128,255), rgba(128,128,128,255), rgba(128,128,128,255)};
	RENDER_NO_TEXTURE = render_texture_create(2, 2, white);
	model_mat = mat4_identity();
}
void render_cleanup(void) {}
void render_set_screen_size(vec2i_t size) { (void)size; }
void render_set_resolution(render_resolution_t res) { (void)res; }
void render_set_post_effect(render_post_effect_t post) { (void)post; }
vec2i_t render_size(void) { return vec2i(1920, 1080); }
void render_frame_prepare(void) {}
void render_frame_end(void) {}
static render_stats_t stats;
const render_stats_t *render_frame_get_stats(void) { return &stats; }
void render_set_view(vec3_t pos, vec3_t angles) { (void)pos; (void)angles; }
void render_set_view_2d(void) {}
void render_set_model_mat(mat4_t *m) { model_mat = *m; }
void render_set_depth_write(bool enabled) { (void)enabled; }
void render_set_depth_test(bool enabled) { (void)enabled; }
void render_set_depth_offset(float offset) { (void)offset; }
void render_set_screen_position(vec2_t pos) { (void)pos; }
void render_set_blend_mode(render_blend_mode_t mode) { (void)mode; }
void render_set_cull_backface(bool enabled) { (void)enabled; }
vec3_t render_transform(vec3_t pos) { return pos; }

static int same_vertex(vertex_t a, vertex_t b) {
	return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z &&
		a.uv.x == b.uv.x && a.uv.y == b.uv.y &&
		a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b && a.color.a == b.color.a;
}

void render_push_tris(tris_t t, uint16_t texture) {
	error_if(texture >= textures_len, "Invalid texture %d", texture);
	for (int i = 0; i < 3; i++) {
		t.vertices[i].pos = vec3_transform(t.vertices[i].pos, &model_mat);
	}
	// object_draw pushes the back of a double-sided polygon as the same
	// triangle with its first two vertices swapped: tris {2,1,0} then {1,2,0};
	// quads {2,1,0},{2,3,1} then {1,2,0},{3,2,1}. Keep one copy and flag it.
	for (uint32_t k = 1; k <= 2 && k <= tris_len; k++) {
		cap_tri_t *c = &tris[tris_len - k];
		if (!c->double_sided && c->texture == texture &&
			same_vertex(c->t.vertices[0], t.vertices[1]) &&
			same_vertex(c->t.vertices[1], t.vertices[0]) &&
			same_vertex(c->t.vertices[2], t.vertices[2])) {
			c->double_sided = 1;
			return;
		}
	}
	if (tris_len >= tris_cap) {
		tris_cap = tris_cap ? tris_cap * 2 : 4096;
		tris = realloc(tris, tris_cap * sizeof(cap_tri_t));
	}
	tris[tris_len++] = (cap_tri_t){.t = t, .texture = texture, .double_sided = 0};
}

static uint32_t sprites_seen = 0;
void render_push_sprite(vec3_t pos, vec2i_t size, rgba_t color, uint16_t texture) {
	(void)pos; (void)size; (void)color; (void)texture;
	sprites_seen++;
}
void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture) {
	(void)pos; (void)size; (void)color; (void)texture;
}
void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) {
	(void)color;
	last_tile = (cap_tile_t){pos, uv_offset, uv_size, size, texture_index};
}

uint16_t render_texture_create(uint32_t width, uint32_t height, rgba_t *pixels) {
	error_if(textures_len >= CAP_TEXTURES_MAX, "CAP_TEXTURES_MAX");
	cap_texture_t *t = &textures[textures_len];
	t->w = width;
	t->h = height;
	t->pixels = malloc(width * height * sizeof(rgba_t));
	memcpy(t->pixels, pixels, width * height * sizeof(rgba_t));
	return textures_len++;
}
vec2i_t render_texture_size(uint16_t texture_index) {
	return vec2i(textures[texture_index].w, textures[texture_index].h);
}
void render_texture_replace_pixels(int16_t texture_index, rgba_t *pixels) {
	cap_texture_t *t = &textures[texture_index];
	memcpy(t->pixels, pixels, t->w * t->h * sizeof(rgba_t));
}
uint16_t render_textures_len(void) { return textures_len; }
void render_textures_reset(uint16_t len) { textures_len = len; }
void render_textures_dump(const char *path) { (void)path; }

// ---------------------------------------------------------------------------
// Output helpers

static char out_dir[1024];
static FILE *manifest;

static const char *out_path(const char *name) {
	static char buf[2048];
	snprintf(buf, sizeof(buf), "%s/%s", out_dir, name);
	return buf;
}

static void write_texture_png(uint16_t index, const char *name) {
	cap_texture_t *t = &textures[index];
	stbi_write_png(out_path(name), t->w, t->h, 4, t->pixels, t->w * 4);
}

// Pack the textures used by tris[first..first+count) into one atlas with a
// replicated border, and write the vertex stream: per vertex 3 f32 position,
// 2 f32 atlas uv, 4 u8 colour (24 bytes); non-indexed triangles.

#define ATLAS_BORDER 4

typedef struct {
	int used;
	int x, y; // texel origin of the texture inside the atlas (after border)
} atlas_slot_t;

static int cmp_height_desc(const void *a, const void *b) {
	uint16_t ia = *(const uint16_t *)a, ib = *(const uint16_t *)b;
	return (int)textures[ib].h - (int)textures[ia].h;
}

static void write_group(const char *name, uint32_t first, uint32_t count) {
	static atlas_slot_t slots[CAP_TEXTURES_MAX];
	memset(slots, 0, sizeof(slots));
	uint16_t order[CAP_TEXTURES_MAX];
	int order_len = 0;
	for (uint32_t i = first; i < first + count; i++) {
		uint16_t tx = tris[i].texture;
		if (!slots[tx].used) {
			slots[tx].used = 1;
			order[order_len++] = tx;
		}
	}
	qsort(order, order_len, sizeof(uint16_t), cmp_height_desc);

	int aw = 256, ah = 0;
	for (;;) {
		int x = 0, y = 0, row_h = 0, ok = 1;
		for (int i = 0; i < order_len; i++) {
			cap_texture_t *t = &textures[order[i]];
			int w = t->w + 2 * ATLAS_BORDER, h = t->h + 2 * ATLAS_BORDER;
			if (w > aw) { ok = 0; break; }
			if (x + w > aw) { x = 0; y += row_h; row_h = 0; }
			slots[order[i]].x = x + ATLAS_BORDER;
			slots[order[i]].y = y + ATLAS_BORDER;
			x += w;
			if (h > row_h) row_h = h;
		}
		ah = y + row_h;
		if (ok && ah <= aw) break;
		aw *= 2;
		error_if(aw > 4096, "atlas too large for %s", name);
	}
	// power-of-two height for mipmapping
	int ph = 1;
	while (ph < ah) ph <<= 1;
	ah = ph;

	rgba_t *atlas = calloc(aw * ah, sizeof(rgba_t));
	for (int i = 0; i < order_len; i++) {
		cap_texture_t *t = &textures[order[i]];
		int ox = slots[order[i]].x, oy = slots[order[i]].y;
		for (int y = -ATLAS_BORDER; y < (int)t->h + ATLAS_BORDER; y++) {
			int sy = y < 0 ? 0 : y >= (int)t->h ? (int)t->h - 1 : y;
			for (int x = -ATLAS_BORDER; x < (int)t->w + ATLAS_BORDER; x++) {
				int sx = x < 0 ? 0 : x >= (int)t->w ? (int)t->w - 1 : x;
				atlas[(oy + y) * aw + (ox + x)] = t->pixels[sy * t->w + sx];
			}
		}
	}
	char fname[256];
	snprintf(fname, sizeof(fname), "%s_atlas.png", name);
	stbi_write_png(out_path(fname), aw, ah, 4, atlas, aw * 4);
	free(atlas);

	snprintf(fname, sizeof(fname), "%s.bin", name);
	FILE *f = fopen(out_path(fname), "wb");
	error_if(!f, "cannot write %s", fname);
	for (uint32_t i = first; i < first + count; i++) {
		cap_tri_t *c = &tris[i];
		atlas_slot_t *s = &slots[c->texture];
		for (int v = 0; v < 3; v++) {
			vertex_t *vx = &c->t.vertices[v];
			float rec[5] = {
				vx->pos.x, vx->pos.y, vx->pos.z,
				(s->x + vx->uv.x) / (float)aw,
				(s->y + vx->uv.y) / (float)ah
			};
			fwrite(rec, sizeof(float), 5, f);
			fwrite(&vx->color, 4, 1, f);
		}
	}
	fclose(f);
	fprintf(manifest, "  \"%s_atlas\": [%d, %d],\n", name, aw, ah);
}

typedef struct {
	char name[17];
	uint32_t first, count, double_sided;
	vec3_t min, max;
} mesh_info_t;

static mesh_info_t mesh_info(const char *name, uint32_t first, uint32_t count) {
	mesh_info_t m = {0};
	snprintf(m.name, sizeof(m.name), "%s", name);
	m.first = first;
	m.count = count;
	m.min = vec3(1e30, 1e30, 1e30);
	m.max = vec3(-1e30, -1e30, -1e30);
	for (uint32_t i = first; i < first + count; i++) {
		m.double_sided += tris[i].double_sided;
		for (int v = 0; v < 3; v++) {
			vec3_t p = tris[i].t.vertices[v].pos;
			m.min = vec3(fminf(m.min.x, p.x), fminf(m.min.y, p.y), fminf(m.min.z, p.z));
			m.max = vec3(fmaxf(m.max.x, p.x), fmaxf(m.max.y, p.y), fmaxf(m.max.z, p.z));
		}
	}
	return m;
}

static void print_mesh(mesh_info_t *m, const char *extra, int last) {
	fprintf(manifest,
		"    {\"name\": \"%s\", \"first\": %u, \"count\": %u, \"double_sided\": %u, "
		"\"min\": [%g, %g, %g], \"max\": [%g, %g, %g]%s}%s\n",
		m->name, m->first, m->count, m->double_sided,
		m->min.x, m->min.y, m->min.z, m->max.x, m->max.y, m->max.z,
		extra ? extra : "", last ? "" : ",");
}

// ---------------------------------------------------------------------------

static void export_ships(void) {
	texture_list_t tl = image_get_compressed_textures("wipeout/common/allsh.cmp");
	Object *models = objects_load("wipeout/common/allsh.prm", tl);

	uint32_t group_first = tris_len;
	mesh_info_t infos[16];
	int n = 0;
	mat4_t identity = mat4_identity();
	for (Object *o = models; o && n < 16; o = o->next) {
		uint32_t first = tris_len;
		object_draw(o, &identity);
		// the bbox is measured from the captured (drawn) triangles
		infos[n] = mesh_info(o->name, first - group_first, tris_len - first);
		n++;
	}
	write_group("ships", group_first, tris_len - group_first);
	fprintf(manifest, "  \"ships\": [\n");
	for (int i = 0; i < n; i++) {
		char extra[128];
		snprintf(extra, sizeof(extra), ", \"model_index\": %d, \"pilot\": %d", i, def_ship_model_to_pilot(i));
		print_mesh(&infos[i], extra, i == n - 1);
	}
	fprintf(manifest, "  ],\n");
	printf("ships: %d models, %u triangles\n", n, tris_len - group_first);
}

static void export_objects(const char *group, const char *prm, const char *cmp) {
	texture_list_t tl = image_get_compressed_textures((char *)cmp);
	Object *objs = objects_load((char *)prm, tl);
	uint32_t group_first = tris_len;
	uint32_t sprites_before = sprites_seen;
	int n = 0;
	fprintf(manifest, "  \"%s\": [\n", group);
	for (Object *o = objs; o; o = o->next) {
		mat4_t m = mat4_identity();
		mat4_set_translation(&m, o->origin);
		uint32_t first = tris_len;
		object_draw(o, &m);
		mesh_info_t info = mesh_info(o->name, first - group_first, tris_len - first);
		print_mesh(&info, NULL, o->next == NULL);
		n++;
	}
	fprintf(manifest, "  ],\n");
	write_group(group, group_first, tris_len - group_first);
	printf("%s: %d objects, %u triangles, %u sprites skipped\n", group, n, tris_len - group_first, sprites_seen - sprites_before);
}

static void export_track(void) {
	track_load("wipeout/track01/");
	uint32_t first = tris_len;
	for (int i = 0; i < g.track.face_count; i++) {
		track_face_t *f = &g.track.faces[i];
		uint16_t tex = texture_from_list(g.track.textures, f->texture);
		for (int k = 0; k < 2; k++) {
			render_push_tris(f->tris[k], tex);
		}
	}
	write_group("track", first, tris_len - first);
	printf("track: %d faces, %d sections\n", g.track.face_count, g.track.section_count);

	// Sections, in the order of the main loop: centre and the base face normal
	FILE *f = fopen(out_path("track_sections.bin"), "wb");
	section_t *s = g.track.sections;
	int n = 0;
	do {
		track_face_t *face = track_section_get_base_face(s);
		float rec[6] = {s->center.x, s->center.y, s->center.z, face->normal.x, face->normal.y, face->normal.z};
		fwrite(rec, sizeof(float), 6, f);
		n++;
		s = s->next;
	} while (s != g.track.sections);
	fclose(f);
	fprintf(manifest, "  \"track_sections\": %d,\n", n);
}

static void export_hud(void) {
	uint16_t speedo = image_get_texture("wipeout/textures/speedo.tim");
	write_texture_png(speedo, "hud_speedo.png");

	texture_list_t wicons = image_get_compressed_textures("wipeout/common/wicons.cmp");
	for (int i = 0; i < wicons.len; i++) {
		char name[64];
		snprintf(name, sizeof(name), "hud_wicon_%d.png", i);
		write_texture_png(texture_from_list(wicons, i), name);
	}
	fprintf(manifest, "  \"wicons\": %d,\n", wicons.len);

	// The font, through ui.c: ui_load() loads drfonts.cmp, then each glyph is
	// drawn once and the tile that ui_draw_text pushes is recorded.
	uint16_t before = render_textures_len();
	ui_load();
	texture_list_t fonts = {.start = before, .len = render_textures_len() - before};
	const char *sizes[3] = {"16", "12", "8"};
	const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ef";
	ui_set_scale(1);
	fprintf(manifest, "  \"font\": {\n");
	for (int s = 0; s < 3; s++) {
		int tex = -1;
		fprintf(manifest, "    \"%s\": {\"glyphs\": {", sizes[s]);
		for (int c = 0; chars[c]; c++) {
			char str[2] = {chars[c], 0};
			ui_draw_text(str, vec2i(0, 0), (ui_text_size_t)s, rgba(128, 128, 128, 255));
			tex = last_tile.texture;
			fprintf(manifest, "%s\"%c\": [%d, %d, %d, %d]", c ? ", " : "", chars[c],
				last_tile.uv_offset.x, last_tile.uv_offset.y, last_tile.uv_size.x, last_tile.uv_size.y);
		}
		char name[64];
		snprintf(name, sizeof(name), "hud_font%s.png", sizes[s]);
		write_texture_png(tex, name);
		fprintf(manifest, "}, \"space\": %d}%s\n", ui_char_width(' ', (ui_text_size_t)s), s < 2 ? "," : "");
	}
	fprintf(manifest, "  },\n");
	// icons: star (lives), from the same cmp
	write_texture_png(texture_from_list(fonts, 9), "hud_icon_star.png");
}

int def_ship_model_to_pilot(int i) {
	// game.c: def.ship_model_to_pilot (def is not linked here)
	static const int map[8] = {6, 4, 7, 1, 5, 2, 3, 0};
	return map[i];
}

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s <data_root> <out_dir>\n", argv[0]);
		return 1;
	}
	snprintf(data_root, sizeof(data_root), "%s", argv[1]);
	snprintf(out_dir, sizeof(out_dir), "%s", argv[2]);

	render_init(vec2i(1920, 1080));
	manifest = fopen(out_path("manifest.json"), "w");
	error_if(!manifest, "cannot write manifest");
	fprintf(manifest, "{\n");

	export_ships();
	export_hud();
	export_track();
	export_objects("scene", "wipeout/track01/scene.prm", "wipeout/track01/scene.cmp");
	export_objects("sky", "wipeout/track01/sky.prm", "wipeout/track01/sky.cmp");

	fprintf(manifest, "  \"sky_y_offset\": -820,\n");
	fprintf(manifest, "  \"source\": \"export_mockup (game loaders: object.c, image.c, track.c, ui.c)\"\n}\n");
	fclose(manifest);
	mem_temp_check();
	return 0;
}
