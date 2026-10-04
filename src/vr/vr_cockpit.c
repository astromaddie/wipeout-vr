#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../mem.h"
#include "../render.h"
#include "../utils.h"
#include "../wipeout/camera.h"
#include "../wipeout/game.h"
#include "../wipeout/hud.h"
#include "../wipeout/image.h"
#include "../wipeout/object.h"
#include "../wipeout/ship.h"
#include "../wipeout/ui.h"
#include "../wipeout/weapon.h"

#include "vr_cockpit.h"
#include "vr_log.h"

// The cockpit as the game sees it. See vr_cockpit.h.

typedef struct {
	tris_t tris;
	uint16_t texture;
} cockpit_tri_t;

typedef struct {
	cockpit_tri_t *tris;
	int count;
} cockpit_mesh_t;

typedef struct {
	bool valid;
	int shape;
	vr_cockpit_trim_t trim;
	cockpit_mesh_t hull; // outside, culled backfaces
	cockpit_mesh_t inside; // wound the other way, dimmed
	cockpit_mesh_t parts; // pre-shaded, drawn double-sided
	cockpit_mesh_t glass;
} ship_cockpit_t;

static ship_cockpit_t cockpits[NUM_PILOTS];

static struct {
	bool active;
	vr_cockpit_settings_t settings;
	vr_cockpit_pose_t pose;
} frame;


// -----------------------------------------------------------------------------
// Load

static void mesh_free(cockpit_mesh_t *m) {
	free(m->tris);
	m->tris = NULL;
	m->count = 0;
}

static void mesh_from(const vr_cockpit_trilist_t *l, cockpit_mesh_t *m, uint16_t texture_for_none) {
	m->count = l->count;
	m->tris = l->count ? malloc((size_t)l->count * sizeof(cockpit_tri_t)) : NULL;
	if (l->count && !m->tris) {
		die("Out of memory building the cockpit");
	}
	for (int i = 0; i < l->count; i++) {
		cockpit_tri_t *t = &m->tris[i];
		for (int k = 0; k < 3; k++) {
			const vr_cockpit_vertex_t *v = &l->tris[i].v[k];
			t->tris.vertices[k] = (vertex_t){
				.pos = {v->pos[0], v->pos[1], v->pos[2]},
				.uv = {v->uv[0], v->uv[1]},
				.color = {v->color[0], v->color[1], v->color[2], v->color[3]}
			};
		}
		t->texture = l->tris[i].texture < 0 ? texture_for_none : (uint16_t)l->tris[i].texture;
	}
}

void vr_cockpit_unload(void) {
	for (int i = 0; i < NUM_PILOTS; i++) {
		mesh_free(&cockpits[i].hull);
		mesh_free(&cockpits[i].inside);
		mesh_free(&cockpits[i].parts);
		mesh_free(&cockpits[i].glass);
		memset(&cockpits[i], 0, sizeof(cockpits[i]));
	}
}

// The triangles object_draw pushes for a model, with the winding it gives them
// (the sprites are not part of the hull)
static int model_triangles(const Object *o, vr_cockpit_trilist_t *out) {
	int count = 0;
	for (int i = 0; i < o->primitives_len; i++) {
		const primitive_t *prm = &o->primitives[i];
		vr_cockpit_tri_t t;
		if (prm->type == PRM_TYPE_TRI || prm->type == PRM_TYPE_QUAD) {
			const primitive_vertex_t *pv = prm->type == PRM_TYPE_TRI ? prm->u.tri.v : prm->u.quad.v;
			int n = prm->type == PRM_TYPE_TRI ? 3 : 4;
			vr_cockpit_vertex_t v[4];
			for (int j = 0; j < n; j++) {
				const vec3_t p = o->vertices[pv[j].coord];
				v[j] = (vr_cockpit_vertex_t){
					.pos = {p.x, p.y, p.z},
					.uv = {pv[j].u, pv[j].v},
					.color = {pv[j].color.r, pv[j].color.g, pv[j].color.b, pv[j].color.a}
				};
			}
			t.texture = prm->u.quad.texture; // tri.texture is the same slot
			const int order[2][3] = {{2, 1, 0}, {2, 3, 1}};
			for (int q = 0; q < (n == 3 ? 1 : 2); q++) {
				for (int k = 0; k < 3; k++) {
					t.v[k] = v[order[q][k]];
				}
				if (out) {
					vr_cockpit_trilist_push(out, &t);
				}
				count++;
			}
		}
	}
	return count;
}

void vr_cockpit_build_ship(int ship_index, const Object *model, const vr_cockpit_image_t *images, int image_count) {
	ship_cockpit_t *c = &cockpits[ship_index];
	mesh_free(&c->hull);
	mesh_free(&c->inside);
	mesh_free(&c->parts);
	mesh_free(&c->glass);
	memset(c, 0, sizeof(*c));
	if (!model) {
		return;
	}

	int tris = model_triangles(model, NULL);
	c->shape = vr_cockpit_hull_shape(model->name, tris);
	if (c->shape < 0) {
		vr_logf("cockpit: ship %d (\"%s\", %d triangles) matches no hull shape: it keeps the flat internal view", ship_index, model->name, tris);
		return;
	}

	vr_cockpit_trilist_t source = {0}, hull = {0}, inside = {0}, parts = {0}, glass = {0};
	model_triangles(model, &source);
	const vr_cockpit_cut_t cut = vr_cockpit_cut();
	for (int i = 0; i < source.count; i++) {
		vr_cockpit_clip_triangle(&source.tris[i], &cut, &hull);
	}
	vr_cockpit_inside_copy(&hull, VR_COCKPIT_INSIDE_DIM, &inside);
	vr_cockpit_sample_trim(source.tris, source.count, images, image_count, &c->trim);
	const uint8_t trim[2][3] = {
		{c->trim.primary[0], c->trim.primary[1], c->trim.primary[2]},
		{c->trim.secondary[0], c->trim.secondary[1], c->trim.secondary[2]}
	};
	vr_cockpit_build_parts(c->shape, trim, &parts, &glass);

	// the untextured polygons of the hull carry texture 0, the white texel, as object_draw has them
	mesh_from(&hull, &c->hull, 0);
	mesh_from(&inside, &c->inside, 0);
	mesh_from(&parts, &c->parts, RENDER_NO_TEXTURE);
	mesh_from(&glass, &c->glass, RENDER_NO_TEXTURE);
	c->valid = true;

	vr_cockpit_trilist_free(&source);
	vr_cockpit_trilist_free(&hull);
	vr_cockpit_trilist_free(&inside);
	vr_cockpit_trilist_free(&parts);
	vr_cockpit_trilist_free(&glass);
	vr_logf("cockpit: ship %d \"%s\": hull shape %d, %d hull + %d inside + %d part + %d glass triangles, trim %02x%02x%02x / %02x%02x%02x%s",
		ship_index, model->name, c->shape, c->hull.count, c->inside.count, c->parts.count, c->glass.count,
		c->trim.primary[0], c->trim.primary[1], c->trim.primary[2],
		c->trim.secondary[0], c->trim.secondary[1], c->trim.secondary[2],
		c->trim.neutral ? " (neutral)" : c->trim.secondary_fallback ? " (no second hue)" : "");
}

void vr_cockpit_load(texture_list_t ship_textures, char *textures_path) {
	vr_cockpit_unload();

	// The ships' textures again, as pixels: the atlas is not read back (lesson 10). Each is
	// copied out of the game's temp memory (8 objects at most) as soon as it is decoded.
	cmp_t *cmp = image_load_compressed(textures_path);
	const uint32_t texture_count = cmp->len;
	const int image_count = (int)ship_textures.start + (int)texture_count;
	vr_cockpit_image_t *views = calloc((size_t)image_count, sizeof(vr_cockpit_image_t));
	if (!views) {
		die("Out of memory sampling the cockpit trim");
	}
	static const uint8_t grey[2 * 2 * 4] = {128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255};
	views[0] = (vr_cockpit_image_t){2, 2, grey}; // RENDER_NO_TEXTURE (a 128 grey): untextured polygons carry texture 0
	for (uint32_t i = 0; i < texture_count; i++) {
		image_t *image = image_load_from_bytes(cmp->entries[i], false);
		size_t bytes = (size_t)image->width * image->height * 4;
		uint8_t *copy = malloc(bytes);
		if (!copy) {
			die("Out of memory sampling the cockpit trim");
		}
		memcpy(copy, image->pixels, bytes);
		views[ship_textures.start + i] = (vr_cockpit_image_t){(int)image->width, (int)image->height, copy};
		mem_temp_free(image);
	}
	mem_temp_free(cmp);

	int built = 0;
	for (int s = 0; s < NUM_PILOTS; s++) {
		vr_cockpit_build_ship(s, g.ships[s].model, views, image_count);
		built += cockpits[s].valid;
	}

	for (uint32_t i = 0; i < texture_count; i++) {
		free((void *)views[ship_textures.start + i].rgba);
	}
	free(views);
	vr_logf("cockpit: %d of %d ships have a cockpit", built, NUM_PILOTS);
}

bool vr_cockpit_hull_triangle(int ship_index, int index, float pos[9]) {
	if (ship_index < 0 || ship_index >= NUM_PILOTS || index < 0 || index >= cockpits[ship_index].hull.count) {
		return false;
	}
	const tris_t *t = &cockpits[ship_index].hull.tris[index].tris;
	for (int k = 0; k < 3; k++) {
		pos[k * 3 + 0] = t->vertices[k].pos.x;
		pos[k * 3 + 1] = t->vertices[k].pos.y;
		pos[k * 3 + 2] = t->vertices[k].pos.z;
	}
	return true;
}

bool vr_cockpit_ship_info(int ship_index, vr_cockpit_ship_info_t *out) {
	if (ship_index < 0 || ship_index >= NUM_PILOTS) {
		return false;
	}
	const ship_cockpit_t *c = &cockpits[ship_index];
	out->valid = c->valid;
	out->shape = c->shape;
	out->trim = c->trim;
	out->hull_tris = c->hull.count;
	out->inside_tris = c->inside.count;
	out->part_tris = c->parts.count;
	out->glass_tris = c->glass.count;
	return true;
}


// -----------------------------------------------------------------------------
// The frame

bool vr_cockpit_active(void) {
	return frame.active;
}

const vr_cockpit_pose_t *vr_cockpit_pose_now(void) {
	return &frame.pose;
}

bool vr_cockpit_begin(camera_t *camera) {
	frame.active = false;
	if (!render_vr_stereo()) {
		return false;
	}
	const vr_cockpit_settings_t *st = (const vr_cockpit_settings_t *)render_vr_cockpit_settings();
	if (!st->enabled) {
		return false;
	}
	ship_t *ship = &g.ships[g.pilot];
	if (!cockpits[g.pilot].valid || flags_not(ship->flags, SHIP_VIEW_INTERNAL) || flags_is(ship->flags, SHIP_IN_RESCUE)) {
		return false;
	}
	// The attract mode's internal camera only gets this far in a stereo frame (vr.diag_attract_stereo)
	if (camera->update_func != camera_update_race_internal && camera->update_func != camera_update_attract_internal) {
		return false;
	}

	frame.settings = *st;
	float eye[3];
	vr_cockpit_eye(st, st->world_scale, eye);
	const float pos[3] = {ship->position.x, ship->position.y, ship->position.z};
	const float angles[3] = {ship->angle.x, ship->angle.y, ship->angle.z};
	vr_cockpit_pose(st->bank, pos, angles, eye, &frame.pose);

	render_vr_cockpit_anchor(
		vec3(frame.pose.eye_world[0], frame.pose.eye_world[1], frame.pose.eye_world[2]),
		vec3(frame.pose.anchor_angles[0], frame.pose.anchor_angles[1], frame.pose.anchor_angles[2]));
	frame.active = true;
	return true;
}


// -----------------------------------------------------------------------------
// Draw

static void push_mesh(const cockpit_mesh_t *m) {
	for (int i = 0; i < m->count; i++) {
		render_push_tris(m->tris[i].tris, m->tris[i].texture);
	}
}

void vr_cockpit_draw(ship_t *ship) {
	if (!frame.active) {
		return;
	}
	const ship_cockpit_t *c = &cockpits[ship - g.ships];
	if (!c->valid) {
		return;
	}

	mat4_t model;
	memcpy(model.m, frame.pose.model, sizeof(model.m));
	render_set_model_mat(&model);

	// 1. The hull, outside and inside: single-sided polygons, so the inside is a dimmed copy wound the other way
	render_set_cull_backface(true);
	push_mesh(&c->hull);
	push_mesh(&c->inside);

	// 2. The authored parts, double-sided
	render_set_cull_backface(false);
	push_mesh(&c->parts);

	// 3. The instrument faces: the textures the 2D section draws into this frame
	for (int i = 0; i < vr_cockpit_face_count(); i++) {
		vr_cockpit_face_info_t f;
		vr_cockpit_face(i, &f);
		vec3_t corners[4];
		for (int k = 0; k < 4; k++) {
			corners[k] = vec3(f.corners[k * 3], f.corners[k * 3 + 1], f.corners[k * 3 + 2]);
		}
		render_vr_face_quad(i, corners, rgba(128, 128, 128, 255));
	}

	// 4. The glass last: a faint tint, blended, and no depth write
	if (frame.settings.glass && c->glass.count) {
		render_set_depth_write(false);
		push_mesh(&c->glass);
		render_set_depth_write(true);
	}

	render_set_cull_backface(true);
}


// -----------------------------------------------------------------------------
// The instrument faces: the game's own 2D calls at texel positions and an
// integer ui_scale (the layouts of docs/COCKPIT_MOCKUP.md, "Asset set")

#define FACE_CLEAR rgba(11, 13, 16, 255) // the mockup's face background

static vec2i_t at(vec2i_t origin, int x, int y) {
	int s = ui_get_scale();
	return vec2i(origin.x + x * s, origin.y + y * s);
}

// A flat quad. RENDER_NO_TEXTURE is a 128 grey that the shader doubles, so `color` is what shows
static void quad(vec2i_t pos, vec2i_t size, rgba_t color) {
	render_push_2d(pos, size, color, RENDER_NO_TEXTURE);
}

// The status slot is a small table, so a later change is one more entry
typedef struct {
	const char *name;
	void (*draw)(ship_t *ship, vec2i_t origin);
} vr_cockpit_status_t;

static void status_shield(ship_t *ship, vec2i_t origin) {
	float left = 0;
	if (flags_is(ship->flags, SHIP_SHIELDED)) {
		left = weapon_shield_time_left(ship) / WEAPON_SHIELD_DURATION;
		left = left < 0 ? 0 : left > 1 ? 1 : left;
	}
	ui_draw_text("SHIELD", at(origin, 2, 2), UI_SIZE_8, left > 0 ? UI_COLOR_DEFAULT : UI_COLOR_ACCENT);
	const int segments = 8;
	int filled = (int)ceilf(segments * left);
	int s = ui_get_scale();
	for (int i = 0; i < segments; i++) {
		// 7.5 HUD pixels apart, 6 wide, 8 tall, at y 13
		vec2i_t pos = vec2i(origin.x + ((4 + 15 * i) * s) / 2, origin.y + 13 * s);
		quad(pos, vec2i(6 * s, 8 * s), i < filled ? rgba(120, 200, 255, 255) : rgba(80, 90, 110, 140));
	}
}

static const vr_cockpit_status_t status_slots[] = {
	{"shield", status_shield},
};
static const vr_cockpit_status_t *status_slot = &status_slots[0];

static bool element_origin(const char *face, const char *element, vec2i_t *origin, int *scale) {
	int x, y;
	if (!vr_cockpit_element(face, element, &x, &y, scale)) {
		return false;
	}
	*origin = vec2i(x, y);
	return true;
}

static void face_left(ship_t *ship) {
	vec2i_t o;
	int scale;

	if (element_origin("left", "lap", &o, &scale)) {
		ui_set_scale(scale);
		int display_lap = max(0, ship->lap + 1);
		ui_draw_text("LAP", at(o, 5, 2), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_number(display_lap, at(o, 2, 12), UI_SIZE_16, UI_COLOR_DEFAULT);
		int width = ui_number_width(display_lap, UI_SIZE_16);
		ui_draw_text("OF", at(o, 2 + width + 2, 20), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_number(NUM_LAPS, at(o, 2 + width + 4 + ui_text_width("OF", UI_SIZE_8), 12), UI_SIZE_16, UI_COLOR_DEFAULT);
	}

	if (g.race_type != RACE_TYPE_TIME_TRIAL && element_origin("left", "position", &o, &scale)) {
		ui_set_scale(scale);
		int w = ui_text_width("POSITION", UI_SIZE_8) + 4;
		ui_draw_text("POSITION", at(o, 2, 2), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_number(ship->position_rank, at(o, w / 2 - ui_number_width(ship->position_rank, UI_SIZE_16) / 2, 12), UI_SIZE_16, UI_COLOR_DEFAULT);
	}

	if (g.race_type == RACE_TYPE_CHAMPIONSHIP && element_origin("left", "lives", &o, &scale)) {
		ui_set_scale(scale);
		for (int i = 0; i < g.lives; i++) {
			ui_draw_icon(UI_ICON_STAR, at(o, 1 + 13 * i, 1), UI_COLOR_DEFAULT);
		}
	}

	if (ship->lap > 0 && element_origin("left", "prevlaps", &o, &scale)) {
		ui_set_scale(scale);
		for (int i = 0; i < ship->lap && i < NUM_LAPS - 1; i++) {
			ui_draw_time(g.lap_times[ship->pilot][i], at(o, 2, 1 + 10 * i), UI_SIZE_8, UI_COLOR_ACCENT);
		}
	}

	if (element_origin("left", "laptime", &o, &scale)) {
		ui_set_scale(scale);
		if (ship->lap >= 0) {
			ui_draw_time(ship->lap_time, at(o, 2, 2), UI_SIZE_16, UI_COLOR_DEFAULT);
		}
		ui_draw_text("LAP RECORD", at(o, 2, 22), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_time(save.highscores[g.race_class][g.circuit][g.highscore_tab].lap_record,
			at(o, 6 + ui_text_width("LAP RECORD", UI_SIZE_8), 22), UI_SIZE_8, UI_COLOR_DEFAULT);
	}
}

static void face_right(ship_t *ship) {
	vec2i_t o;
	int scale;

	if (element_origin("right", "speedo", &o, &scale)) {
		ui_set_scale(scale);
		int speed = (g.camera.update_func == camera_update_attract_internal) ? ship->speed * 7 : ship->speed;
		hud_draw_speedo_at(at(o, 2, 0), speed, ship->thrust_mag);
	}

	if (element_origin("right", "shield", &o, &scale)) {
		ui_set_scale(scale);
		status_slot->draw(ship, o);
	}
}

static void face_weapon(ship_t *ship) {
	vec2i_t o;
	int scale;
	if (!element_origin("weapon", "weapon", &o, &scale)) {
		return;
	}
	ui_set_scale(scale);

	// the frame, 1 HUD pixel in and 34 across, a line 0.8 pixel thick (centred on the edge)
	int t = max(1, (scale * 4 + 2) / 5);
	vec2i_t a = at(o, 1, 1);
	int side = 34 * scale;
	rgba_t line = rgba(246, 196, 24, 140);
	quad(vec2i(a.x - t / 2, a.y - t / 2), vec2i(side + t, t), line);
	quad(vec2i(a.x - t / 2, a.y + side - t / 2), vec2i(side + t, t), line);
	quad(vec2i(a.x - t / 2, a.y - t / 2), vec2i(t, side + t), line);
	quad(vec2i(a.x + side - t / 2, a.y - t / 2), vec2i(t, side + t), line);

	if (ship->weapon_type != WEAPON_TYPE_NONE) {
		render_push_2d(at(o, 2, 2), ui_scaled(vec2i(32, 32)), rgba(128, 128, 128, 255), hud_weapon_icon(ship->weapon_type));
	}
}

bool vr_cockpit_hud_draw(ship_t *ship) {
	if (!frame.active) {
		return false;
	}

	// Drawn once, here, in the frame's 2D section: the replay puts each face into its
	// texture once, before the eyes, and both eyes sample it
	const int saved_scale = ui_get_scale();
	void (*const draw[VR_COCKPIT_FACE_MAX])(ship_t *) = {face_left, face_right, face_weapon};
	for (int i = 0; i < vr_cockpit_face_count() && i < VR_COCKPIT_FACE_MAX; i++) {
		vr_cockpit_face_info_t f;
		vr_cockpit_face(i, &f);
		render_vr_face_begin(i, f.tex_w, f.tex_h, FACE_CLEAR);
		draw[i](ship);
		render_vr_face_end();
	}
	ui_set_scale(saved_scale);

	// What stays on the HUD quad: WRONG WAY and the target reticle (docs/COCKPIT_MOCKUP.md, open question 4)
	hud_draw_overlay(ship);
	return true;
}
