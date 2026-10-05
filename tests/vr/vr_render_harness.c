// Host acceptance harness for the stereo renderer.
//
// Drives the real renderer (src/render_gl.c with WIPEOUT_VR, the command list,
// the replay, the HUD alpha solve, the dumps) with a synthetic scene and a fake
// view provider standing in for the runtime. An offscreen GL context (a hidden
// SDL window; run it under Xvfb with Mesa llvmpipe) supplies the GPU. It never
// touches game data: the scene is a ground strip, coloured marker cubes, a sky,
// a sprite and 2D quads at partial alpha, built from render_* calls the way the
// game builds them.
//
//   vr_render_harness [output-dir]     (default ./vr-harness)
//
// Writes the eye and HUD dumps (BMP from the product's own dump path, PNG
// copies for viewing) under the output directory, one folder per head yaw,
// and exits non-zero when an assertion fails.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL.h>
#include <GL/glew.h>

#include <stb_image_write.h> // the implementation is image.c's (the cockpit tests link the game's image loader)

#include "sfvr_test.h"
#include "types.h"
#include "render.h"
#include "system.h"
#include "vr_render.h"
#include "vr_perf.h"

double system_cycle_time(void) {
	return 0;
}

#define PI_F 3.14159265f
#define PI 3.14159265358979
#define UI_W 960
#define UI_H 540
#define EYE_W 640
#define EYE_H 640
#define FRAME_NS 11111111LL
#define WORLD_SCALE 128.0f
#define IPD 0.064f

static const char *out_dir = "vr-harness";
static SDL_Window *window;
static SDL_GLContext gl_context;


// -----------------------------------------------------------------------------
// The fake runtime

typedef struct {
	GLuint tex;
	uint32_t w, h;
	bool acquired;
} fake_surface_t;

static struct {
	fake_surface_t surface[VR_SURFACE_COUNT];
	vr_frame_t frame;
	bool renders;
	vr_layers_t layers; // what the last frame submitted
	int submits;
	int acquires, releases;
	int64_t time_ns;

	// the head
	float yaw; // radians, positive turns left
	float roll;
	float pos[3];
	bool symmetric_fov;
	float ipd; // metres
	bool custom_fov; // the cockpit comparison: both eyes get fov_angles
	float fov_angles[4];
} fake;

static GLuint make_image(uint32_t w, uint32_t h) {
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	return t;
}

static void fake_init(void) {
	memset(&fake, 0, sizeof(fake));
	fake.renders = true;
	fake.pos[1] = 1.6f;
	fake.ipd = IPD;
	fake.time_ns = 5000000000LL;
	uint32_t sizes[VR_SURFACE_COUNT][2] = { {EYE_W, EYE_H}, {EYE_W, EYE_H}, {UI_W, UI_H}, {UI_W, UI_H} };
	for (int s = 0; s < VR_SURFACE_COUNT; s++) {
		fake.surface[s].w = sizes[s][0];
		fake.surface[s].h = sizes[s][1];
		fake.surface[s].tex = make_image(sizes[s][0], sizes[s][1]);
	}
}

// Between tests: counters, head and fov back to their defaults. The images stay.
static void fake_reset(void) {
	fake.renders = true;
	fake.acquires = fake.releases = 0;
	fake.submits = 0;
	fake.yaw = fake.roll = 0;
	fake.pos[0] = fake.pos[2] = 0;
	fake.pos[1] = 1.6f;
	fake.symmetric_fov = false;
	fake.ipd = IPD;
	fake.custom_fov = false;
	memset(&fake.layers, 0, sizeof(fake.layers));
}

static void quat_roll(float angle, float q[4]) {
	q[0] = q[1] = 0;
	q[2] = sinf(angle / 2);
	q[3] = cosf(angle / 2);
}

static void quat_mul(const float a[4], const float b[4], float out[4]) {
	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// The fov of an eye; the asymmetric pair has the left eye seeing more to the left
static void eye_fov(int eye, float fov[4]) {
	if (fake.custom_fov) {
		memcpy(fov, fake.fov_angles, 4 * sizeof(float));
	}
	else if (fake.symmetric_fov) {
		fov[0] = -1.0f; fov[1] = 1.0f; fov[2] = 1.0f; fov[3] = -1.0f;
	}
	else if (eye == 0) {
		fov[0] = -0.9f; fov[1] = 0.7f; fov[2] = 0.8f; fov[3] = -0.8f;
	}
	else {
		fov[0] = -0.7f; fov[1] = 0.9f; fov[2] = 0.8f; fov[3] = -0.8f;
	}
}

static const vr_frame_t *fake_frame(void *user, bool *renders) {
	(void)user;
	*renders = fake.renders;

	vr_frame_t *f = &fake.frame;
	memset(f, 0, sizeof(*f));
	f->predicted_display_time = fake.time_ns;
	f->predicted_display_period = FRAME_NS;
	f->should_render = fake.renders;
	f->views_valid = true;

	float yq[4], rq[4], q[4];
	vr_quat_from_yaw(fake.yaw, yq);
	quat_roll(fake.roll, rq);
	quat_mul(yq, rq, q);
	float c = cosf(fake.yaw), s = sinf(fake.yaw);
	for (int e = 0; e < 2; e++) {
		vr_view_t *v = &f->views[e];
		memcpy(v->pose.orientation, q, sizeof(q));
		// the eye offset (+-ipd/2 along the head's own x) turned by the yaw about +Y
		float half = (e == 0 ? -0.5f : 0.5f) * fake.ipd;
		v->pose.position[0] = fake.pos[0] + c * half;
		v->pose.position[1] = fake.pos[1];
		v->pose.position[2] = fake.pos[2] - s * half;
		eye_fov(e, v->fov);
	}
	return f;
}

static bool fake_acquire(void *user, vr_surface_t surface, vr_target_t *out) {
	(void)user;
	fake_surface_t *s = &fake.surface[surface];
	CHECK(!s->acquired);
	s->acquired = true;
	fake.acquires++;
	out->image = s->tex;
	out->width = s->w;
	out->height = s->h;
	return true;
}

static void fake_release(void *user, vr_surface_t surface) {
	(void)user;
	CHECK(fake.surface[surface].acquired);
	fake.surface[surface].acquired = false;
	fake.releases++;
}

static void fake_submit(void *user, const vr_layers_t *layers) {
	(void)user;
	fake.layers = *layers;
	fake.submits++;
}


// -----------------------------------------------------------------------------
// The synthetic scene, built with the render_* calls the game uses

static uint16_t sprite_texture;

static vec3_t cam_pos = {0, -300, 0}; // 300 units above the ground (the world is Y-down)
static vec3_t cam_angles = {0, 0, 0};

typedef struct {
	vec3_t origin;
	float radius;
	rgba_t color;
} marker_t;

static const marker_t markers[] = {
	{ {0, -300, 1000}, 100, {255, 0, 0, 255} }, // ahead, red
	{ {1000, -300, 0}, 100, {0, 255, 0, 255} }, // right, green
	{ {-1000, -300, 0}, 100, {0, 0, 255, 255} }, // left, blue
	{ {0, -300, -1000}, 100, {255, 255, 0, 255} }, // behind, yellow
	{ {150, -300, 400}, 40, {255, 128, 0, 255} }, // near and a little right, orange: the disparity probe
};
static const vec3_t sprite_pos = {-500, -300, 1500}; // ahead and to the left
#define SPRITE_SIZE 240

static const vec3_t far_cube_origin = {0, -5000, 56000}; // 56000 units away: inside the fade (48000 to 64000)
#define FAR_CUBE_HALF 600

static const rgba_t sky_color = {80, 140, 255, 255};
static const rgba_t ground_color = {70, 70, 70, 255};

static void push_triangle_both_sides(vec3_t a, vec3_t b, vec3_t c, rgba_t color) {
	vertex_t va = { .pos = a, .uv = {0, 0}, .color = color };
	vertex_t vb = { .pos = b, .uv = {1, 0}, .color = color };
	vertex_t vc = { .pos = c, .uv = {0, 1}, .color = color };
	render_push_tris((tris_t){ .vertices = {va, vb, vc} }, RENDER_NO_TEXTURE);
	render_push_tris((tris_t){ .vertices = {va, vc, vb} }, RENDER_NO_TEXTURE);
}

static void push_quad(vec3_t a, vec3_t b, vec3_t c, vec3_t d, rgba_t color) {
	push_triangle_both_sides(a, b, c, color);
	push_triangle_both_sides(c, b, d, color);
}

static void push_cube(vec3_t o, float h, rgba_t color, const rgba_t *face_colors) {
	vec3_t p[8];
	for (int i = 0; i < 8; i++) {
		p[i] = vec3(o.x + ((i & 1) ? h : -h), o.y + ((i & 2) ? h : -h), o.z + ((i & 4) ? h : -h));
	}
	static const int faces[6][4] = { {0,1,2,3}, {4,5,6,7}, {0,1,4,5}, {2,3,6,7}, {0,2,4,6}, {1,3,5,7} };
	for (int f = 0; f < 6; f++) {
		push_quad(p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], p[faces[f][3]], face_colors ? face_colors[f] : color);
	}
}

typedef enum {
	SKY_PLAIN,
	SKY_FACES // a small cube with a colour per face, for the sky-at-infinity test
} sky_style_t;

static const rgba_t sky_faces[6] = { // -z, +z, -y, +y, -x, +x
	{200, 0, 0, 255}, {0, 200, 200, 255}, {255, 255, 255, 255}, {40, 40, 40, 255}, {0, 200, 0, 255}, {200, 0, 200, 255}
};

// scene_draw(): the sky follows the camera; the objects are culled
static void scene_draw(bool with_sky, sky_style_t style, bool objects, bool far_cube) {
	if (with_sky) {
		render_set_depth_write(false);
		mat4_t m = mat4_identity();
		mat4_set_translation(&m, cam_pos);
		render_vr_sky(true);
		render_set_model_mat(&m);
		float half = style == SKY_FACES ? 3000.0f : 15000.0f; // inside the fade range
		push_cube(vec3(0, 0, 0), half, sky_color, style == SKY_FACES ? sky_faces : NULL);
		render_vr_sky(false);
		render_set_depth_write(true);
	}

	mat4_t id = mat4_identity();
	if (far_cube) {
		render_set_model_mat(&id);
		push_cube(far_cube_origin, FAR_CUBE_HALF, rgba(255, 0, 0, 255), NULL);
	}
	if (!objects) {
		return;
	}
	render_set_model_mat(&id);

	// Objects: the game's forward half-space cull, or distance only in VR
	mat4_t rot = mat4_identity();
	mat4_set_yaw_pitch_roll(&rot, cam_angles);
	vec3_t fwd = rot.basis.forward.vec3;
	for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
		vec3_t diff = vec3_sub(cam_pos, markers[i].origin);
		float cam_dot = vec3_dot(diff, fwd);
		float dist_sq = vec3_dot(diff, diff);
		if (render_vr_wide_cull()) {
			cam_dot = -1;
		}
		if (cam_dot < markers[i].radius && dist_sq < (RENDER_FADEOUT_FAR * RENDER_FADEOUT_FAR)) {
			push_cube(markers[i].origin, markers[i].radius, markers[i].color, NULL);
		}
	}

	// The track: a long strip, drawn with face culling off
	render_set_cull_backface(false);
	push_quad(vec3(-20000, 0, -20000), vec3(20000, 0, -20000), vec3(-20000, 0, 20000), vec3(20000, 0, 20000), ground_color);
	render_set_cull_backface(true);

	// A particle: additive, no depth write, depth offset, billboarded
	render_set_model_mat(&id);
	render_set_depth_write(false);
	render_set_blend_mode(RENDER_BLEND_LIGHTER);
	render_set_depth_offset(-32.0);
	render_push_sprite(sprite_pos, vec2i(SPRITE_SIZE, SPRITE_SIZE), rgba(128, 128, 128, 255), sprite_texture);
	render_set_depth_offset(0.0);
	render_set_depth_write(true);
	render_set_blend_mode(RENDER_BLEND_NORMAL);
}

static vec3_t reticle_probe[3]; // render_transform() results in the 2D segment
static vec3_t reticle_world[3];

static void synthetic_hud_draw(void) {
	render_set_view_2d();
	// Partial alpha: white 50%, red 100%, green 25%
	render_push_2d(vec2i(100, 100), vec2i(200, 100), rgba(255, 255, 255, 128), RENDER_NO_TEXTURE);
	render_push_2d(vec2i(400, 100), vec2i(200, 100), rgba(255, 0, 0, 255), RENDER_NO_TEXTURE);
	render_push_2d(vec2i(100, 300), vec2i(200, 100), rgba(0, 255, 0, 64), RENDER_NO_TEXTURE);

	// hud_draw_target_icon projects a world point with render_transform()
	for (int i = 0; i < 3; i++) {
		reticle_probe[i] = render_transform(reticle_world[i]);
	}
}

typedef struct {
	bool stereo; // mark the frame as the race camera
	bool sky;
	sky_style_t sky_style;
	bool objects;
	bool shake; // a camera shake of the kind race.c passes to render_set_screen_position
	bool hud;
	bool far_cube; // a red cube inside the distance fade (the fade-per-eye probe)
} frame_opts_t;

static const frame_opts_t race_frame = { .stereo = true, .sky = true, .sky_style = SKY_PLAIN, .objects = true, .shake = true, .hud = true };

// One game frame: render_frame_prepare() .. render_frame_end(), as system_update does
static void run_frame(const frame_opts_t *o) {
	render_frame_prepare();

	if (o->stereo) {
		render_vr_race_view(true);
	}
	render_set_view(cam_pos, cam_angles);
	render_set_screen_position(o->shake ? vec2(0.3f, 0.3f) : vec2(0, 0));
	scene_draw(o->sky, o->sky_style, o->objects, o->far_cube);
	render_set_screen_position(vec2(0, 0));
	if (o->hud) {
		synthetic_hud_draw();
	}

	render_frame_end();
	fake.time_ns += FRAME_NS;
}


// -----------------------------------------------------------------------------
// Pixels

static uint8_t *read_texture(GLuint tex, uint32_t w, uint32_t h) {
	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	uint8_t *px = malloc((size_t)w * h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	return px;
}

static const uint8_t *px_at(const uint8_t *img, uint32_t w, uint32_t h, int x, int y_top) {
	return &img[((size_t)(h - 1 - y_top) * w + x) * 4];
}

typedef struct {
	int count;
	double cx, cy; // centroid, y from the top
	int min_x, max_x, min_y, max_y;
} blob_t;

static blob_t find_colour(const uint8_t *img, uint32_t w, uint32_t h, int r, int g, int b, int tol) {
	blob_t out = { .min_x = 1 << 30, .min_y = 1 << 30, .max_x = -1, .max_y = -1 };
	double sx = 0, sy = 0;
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			const uint8_t *p = px_at(img, w, h, x, y);
			if (abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol) {
				out.count++;
				sx += x;
				sy += y;
				if ((int)x < out.min_x) out.min_x = x;
				if ((int)x > out.max_x) out.max_x = x;
				if ((int)y < out.min_y) out.min_y = y;
				if ((int)y > out.max_y) out.max_y = y;
			}
		}
	}
	if (out.count) {
		out.cx = sx / out.count;
		out.cy = sy / out.count;
	}
	return out;
}

// The recentre the renderer is expected to have applied: the head's yaw and
// position at the last mono->stereo switch or request. The harness keeps its own
// model of it so the expectation does not come from vr_math.
static float rc_yaw = 0;
static float rc_origin[3] = {0, 1.6f, 0};

static void model_recentre(void) {
	rc_yaw = fake.yaw;
	memcpy(rc_origin, fake.pos, sizeof(rc_origin));
}

// Where a game-space point lands in an eye image, worked out here from first
// principles (head pose in LOCAL space, the game camera at the recentre origin,
// the anchor looking along +Z with the world Y-down), not through vr_math.
static void expect_pixel(int eye, vec3_t world, double *px, double *py) {
	float c = cosf(fake.yaw), s = sinf(fake.yaw);
	float half = (eye == 0 ? -0.5f : 0.5f) * IPD;
	// the eye in LOCAL space (the head's own +x turned by its yaw about +Y), minus the recentre origin
	float lx = fake.pos[0] + c * half - rc_origin[0];
	float ly = fake.pos[1] - rc_origin[1];
	float lz = fake.pos[2] - s * half - rc_origin[2];
	// undo the recentre's yaw: a rotation by -rc_yaw about +Y
	float cr = cosf(rc_yaw), sr = sinf(rc_yaw);
	float ox = cr * lx - sr * lz, oy = ly, oz = sr * lx + cr * lz;
	// into game axes: x right, y down, z forward (OpenXR x right, y up, -z forward)
	vec3_t eye_game = vec3(cam_pos.x + ox * WORLD_SCALE, cam_pos.y - oy * WORLD_SCALE, cam_pos.z - oz * WORLD_SCALE);
	vec3_t d = vec3_sub(world, eye_game);
	// back to OpenXR axes, then into the eye's frame (the head's yaw relative to the recentre)
	float rel = fake.yaw - rc_yaw;
	float cc = cosf(rel), ss = sinf(rel);
	float dx = d.x, dy = -d.y, dz = -d.z;
	float vx = cc * dx - ss * dz;
	float vy = dy;
	float vz = ss * dx + cc * dz;
	float fov[4];
	eye_fov(eye, fov);
	float tl = tanf(fov[0]), tr = tanf(fov[1]), tu = tanf(fov[2]), td = tanf(fov[3]);
	double ndc_x = (2 * (vx / -vz) - (tr + tl)) / (tr - tl);
	double ndc_y = (2 * (vy / -vz) - (tu + td)) / (tu - td);
	*px = (ndc_x + 1) / 2 * EYE_W;
	*py = (1 - ndc_y) / 2 * EYE_H;
}

static void write_png(const char *dir, const char *name, const uint8_t *rgba, uint32_t w, uint32_t h, int comp) {
	char path[600];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	uint8_t *flipped = malloc((size_t)w * h * comp);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			const uint8_t *src = &rgba[((size_t)(h - 1 - y) * w + x) * 4];
			for (int c = 0; c < comp; c++) {
				flipped[((size_t)y * w + x) * comp + c] = src[c];
			}
		}
	}
	stbi_write_png(path, w, h, comp, flipped, w * comp);
	free(flipped);
}

// The HUD composited over mid-grey, to look at
static void write_hud_over_grey(const char *dir, const uint8_t *hud, uint32_t w, uint32_t h) {
	uint8_t *over = malloc((size_t)w * h * 4);
	for (size_t i = 0; i < (size_t)w * h; i++) {
		float a = hud[i * 4 + 3] / 255.0f;
		for (int c = 0; c < 3; c++) {
			over[i * 4 + c] = (uint8_t)(hud[i * 4 + c] * a + 96 * (1 - a) + 0.5f);
		}
		over[i * 4 + 3] = 255;
	}
	write_png(dir, "hud-over-grey.png", over, w, h, 3);
	free(over);
}

static void write_alpha_png(const char *dir, const uint8_t *hud, uint32_t w, uint32_t h) {
	uint8_t *a = malloc((size_t)w * h * 4);
	for (size_t i = 0; i < (size_t)w * h; i++) {
		a[i * 4] = a[i * 4 + 1] = a[i * 4 + 2] = hud[i * 4 + 3];
		a[i * 4 + 3] = 255;
	}
	write_png(dir, "hud-alpha.png", a, w, h, 3);
	free(a);
}

static int file_size(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 ? (int)st.st_size : -1;
}

static void make_dir(const char *path) {
	char buf[600];
	snprintf(buf, sizeof(buf), "%s", path);
	for (char *p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			mkdir(buf, 0755);
			*p = '/';
		}
	}
	mkdir(buf, 0755);
}


// -----------------------------------------------------------------------------
// Set up: a window, the renderer attached to the fake runtime

static vr_render_config_t make_config(const char *dump_dir, bool timing) {
	vr_settings_t s;
	memset(&s, 0, sizeof(s));
	vr_settings_defaults(&s);
	vr_settings_set(&s, "world_scale", WORLD_SCALE);
	vr_render_config_t c;
	memset(&c, 0, sizeof(c));
	c.provider.frame = fake_frame;
	c.provider.acquire = fake_acquire;
	c.provider.release = fake_release;
	c.provider.submit = fake_submit;
	vr_render_settings_from(&c.settings, &s);
	c.settings.ui_width = UI_W;
	c.settings.ui_height = UI_H;
	c.settings.timing = timing;
	c.dump_dir = dump_dir;
	vr_settings_free(&s);
	return c;
}

static void glerrors(const char *when) {
	GLenum e;
	int n = 0;
	while ((e = glGetError()) != GL_NO_ERROR && n++ < 8) {
		printf("  FAIL GL error 0x%x %s\n", e, when);
		sfvr_test_failures++;
	}
	sfvr_test_checks++;
}

static const frame_opts_t mono_frame = { .stereo = false, .sky = true, .sky_style = SKY_PLAIN, .objects = true, .shake = true, .hud = true };


// -----------------------------------------------------------------------------
// 1. The mono replay is the flat renderer

static void test_mono_replay_is_the_flat_renderer(void) {
	printf("mono replay against the flat renderer\n");

	fake_reset();
	vr_render_config_t cfg = make_config(NULL, false);
	vr_render_attach(&cfg);
	cam_angles = vec3(0.1f, 0.4f, 0);
	run_frame(&mono_frame);
	CHECK(fake.layers.panel);
	CHECK(!fake.layers.projection);
	CHECK(!fake.layers.hud);
	glerrors("after the mono frame");
	uint8_t *panel = read_texture(fake.surface[VR_SURFACE_PANEL].tex, UI_W, UI_H);
	const vr_render_frame_info_t *info = vr_render_frame_info();
	CHECK_INT(info->mode, VR_MODE_MONO);
	CHECK(info->sprites == 1);
	CHECK(info->triangles > 100);
	printf("  recorded %u commands, %u triangles, %u sprite, %u draw calls\n", info->commands, info->triangles, info->sprites, info->draw_calls);

	// The same calls through the flat path: detach, then the default framebuffer
	vr_render_detach();
	render_set_screen_size(vec2i(UI_W, UI_H));
	run_frame(&mono_frame);
	uint8_t *flat = malloc((size_t)UI_W * UI_H * 4);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glReadBuffer(GL_BACK);
	glReadPixels(0, 0, UI_W, UI_H, GL_RGBA, GL_UNSIGNED_BYTE, flat);
	glerrors("after the flat frame");

	size_t different = 0, off_by_more = 0;
	int worst = 0;
	for (size_t i = 0; i < (size_t)UI_W * UI_H; i++) {
		int d = 0;
		for (int c = 0; c < 3; c++) {
			int e = abs((int)panel[i * 4 + c] - (int)flat[i * 4 + c]);
			if (e > d) d = e;
		}
		if (d) different++;
		if (d > 2) off_by_more++;
		if (d > worst) worst = d;
	}
	printf("  %zu of %d pixels differ from the flat renderer (worst channel error %d)\n", different, UI_W * UI_H, worst);
	CHECK_INT(off_by_more, 0);
	CHECK(different * 1000 < (size_t)UI_W * UI_H); // exact, or within a few edge pixels

	// the frame is not blank: sky, ground and the sprite are all in it
	blob_t sky = find_colour(panel, UI_W, UI_H, sky_color.r, sky_color.g, sky_color.b, 6);
	blob_t ground = find_colour(panel, UI_W, UI_H, ground_color.r, ground_color.g, ground_color.b, 6);
	CHECK(sky.count > 1000);
	CHECK(ground.count > 1000);
	write_png(out_dir, "mono-panel.png", panel, UI_W, UI_H, 3);
	write_png(out_dir, "mono-flat-reference.png", flat, UI_W, UI_H, 3);
	free(panel);
	free(flat);
}


// -----------------------------------------------------------------------------
// 2. Recording issues no draw GL

static void test_recording_draws_nothing(void) {
	printf("recording issues no draw GL\n");
	fake_reset();
	vr_render_config_t cfg = make_config(NULL, false);
	vr_render_attach(&cfg);
	cam_angles = vec3(0, 0, 0);

	// A canary framebuffer bound while the game "runs": any draw lands in it
	GLuint canary_tex, canary_fbo;
	glGenTextures(1, &canary_tex);
	glBindTexture(GL_TEXTURE_2D, canary_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &canary_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, canary_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, canary_tex, 0);
	glViewport(0, 0, 64, 64);
	glClearColor(1, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	render_frame_prepare();
	render_vr_race_view(true);
	render_set_view(cam_pos, cam_angles);
	scene_draw(true, SKY_PLAIN, true, false);
	synthetic_hud_draw();
	// the game has run; nothing has been drawn yet
	uint8_t px[64 * 64 * 4];
	glBindFramebuffer(GL_FRAMEBUFFER, canary_fbo);
	glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int touched = 0;
	for (int i = 0; i < 64 * 64; i++) {
		if (px[i * 4] != 255 || px[i * 4 + 1] != 0 || px[i * 4 + 2] != 255) {
			touched++;
		}
	}
	CHECK_INT(touched, 0);
	// and the eye images are untouched too (still the zeros they were made with)
	CHECK_INT(fake.acquires, 0);
	render_frame_end();
	CHECK(fake.acquires > 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &canary_fbo);
	vr_render_detach();
	fake.time_ns += FRAME_NS;
}


// -----------------------------------------------------------------------------
// 3. Stereo frames

typedef struct {
	uint8_t *eye[2];
	uint8_t *hud;
} shot_t;

static shot_t grab(void) {
	shot_t s;
	s.eye[0] = read_texture(fake.surface[VR_SURFACE_EYE0].tex, EYE_W, EYE_H);
	s.eye[1] = read_texture(fake.surface[VR_SURFACE_EYE1].tex, EYE_W, EYE_H);
	s.hud = read_texture(fake.surface[VR_SURFACE_HUD].tex, UI_W, UI_H);
	return s;
}

static void free_shot(shot_t *s) {
	free(s->eye[0]);
	free(s->eye[1]);
	free(s->hud);
}

// PNG copies of what the eyes and the HUD showed, for looking at
static void save_shot(const char *name, const shot_t *s) {
	char dir[600];
	snprintf(dir, sizeof(dir), "%s/%s", out_dir, name);
	make_dir(dir);
	write_png(dir, "eye0.png", s->eye[0], EYE_W, EYE_H, 3);
	write_png(dir, "eye1.png", s->eye[1], EYE_W, EYE_H, 3);
	write_png(dir, "hud-raw-rgba.png", s->hud, UI_W, UI_H, 4);
	write_hud_over_grey(dir, s->hud, UI_W, UI_H);
	write_alpha_png(dir, s->hud, UI_W, UI_H);
}

static void expect_marker(const uint8_t *eye, int which_eye, const marker_t *m, const char *what, double tol_px) {
	blob_t b = find_colour(eye, EYE_W, EYE_H, m->color.r, m->color.g, m->color.b, 12);
	double ex, ey;
	expect_pixel(which_eye, m->origin, &ex, &ey);
	printf("  %-7s eye%d: %5d px, centroid (%.1f, %.1f), expected (%.1f, %.1f)\n", what, which_eye, b.count, b.cx, b.cy, ex, ey);
	CHECK(b.count > 200);
	CHECK(fabs(b.cx - ex) <= tol_px);
	CHECK(fabs(b.cy - ey) <= tol_px);
	CHECK(b.min_x > 5 && b.max_x < EYE_W - 5); // on screen, with room around it
}

static void expect_absent(const uint8_t *eye, const marker_t *m, const char *what) {
	blob_t b = find_colour(eye, EYE_W, EYE_H, m->color.r, m->color.g, m->color.b, 12);
	printf("  %-7s absent: %d px\n", what, b.count);
	CHECK(b.count < 20);
}

static void expect_sky(const uint8_t *eye, const char *what) {
	// the sky above the horizon: the top rows are all sky, so it is drawn there too
	int sky_rows = 0;
	for (int y = 0; y < 40; y++) {
		const uint8_t *p = px_at(eye, EYE_W, EYE_H, EYE_W / 2, y);
		if (abs(p[0] - sky_color.r) <= 8 && abs(p[1] - sky_color.g) <= 8 && abs(p[2] - sky_color.b) <= 8) {
			sky_rows++;
		}
	}
	printf("  %-7s sky in the top 40 rows of the centre column: %d\n", what, sky_rows);
	CHECK(sky_rows >= 38);
}

static void check_hud_alpha(const uint8_t *hud) {
	// The authored alpha comes back, to within the 8-bit round trip of the solve
	struct { int x, y; int r, g, b, a; const char *name; } probes[] = {
		{ 200, 150, 255, 255, 255, 128, "white 50%" },
		{ 500, 150, 255, 0, 0, 255, "red 100%" },
		{ 200, 350, 0, 255, 0, 64, "green 25%" },
		{ 700, 450, 0, 0, 0, 0, "empty" },
	};
	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
		const uint8_t *p = px_at(hud, UI_W, UI_H, probes[i].x, probes[i].y);
		printf("  hud %-10s rgba(%3d,%3d,%3d,%3d), authored alpha %3d\n", probes[i].name, p[0], p[1], p[2], p[3], probes[i].a);
		CHECK(abs(p[3] - probes[i].a) <= 3);
		if (probes[i].a > 0) {
			CHECK(abs(p[0] - probes[i].r) <= 10);
			CHECK(abs(p[1] - probes[i].g) <= 10);
			CHECK(abs(p[2] - probes[i].b) <= 10);
		}
	}
	size_t clear = 0;
	for (size_t i = 0; i < (size_t)UI_W * UI_H; i++) {
		if (hud[i * 4 + 3] <= 2) clear++;
	}
	printf("  hud: %.1f%% of the pixels are transparent\n", 100.0 * clear / ((double)UI_W * UI_H));
	CHECK(clear > (size_t)UI_W * UI_H * 80 / 100);
}

// A stereo race frame with the head turned `yaw_left_deg` to the left
static void stereo_frame(float yaw_left_deg, const char *dump_name) {
	if (dump_name) {
		char dir[600];
		snprintf(dir, sizeof(dir), "%s/%s", out_dir, dump_name);
		make_dir(dir);
		vr_render_dump_request((int)vr_render_frame_info()->frames_replayed + 1, dir);
	}
	fake.yaw = yaw_left_deg * PI_F / 180.0f;
	run_frame(&race_frame);
	CHECK(fake.layers.projection);
	CHECK(fake.layers.hud);
	CHECK(!fake.layers.panel);
}

// The first column where the colour is the +x face's purple, in the middle row
static int sky_edge(const uint8_t *eye) {
	for (int x = 0; x < EYE_W; x++) {
		const uint8_t *p = px_at(eye, EYE_W, EYE_H, x, EYE_H / 2);
		if (abs(p[0] - 200) <= 12 && p[1] <= 12 && abs(p[2] - 200) <= 12) {
			return x;
		}
	}
	return -1;
}

static void test_stereo_frames(void) {
	printf("stereo frames\n");
	fake_reset();
	vr_render_config_t cfg = make_config(NULL, true);
	vr_render_attach(&cfg);
	cam_angles = vec3(0, 0, 0);
	for (int i = 0; i < 3; i++) {
		reticle_world[i] = vec3(0, -300, 1000);
	}
	reticle_world[1] = vec3(1000 * sinf(10 * PI_F / 180), -300, 1000 * cosf(10 * PI_F / 180)); // 10 degrees right
	reticle_world[2] = vec3(0, -300, -1000); // behind the head

	// A mono frame first (the title screen): the panel, fading in from black
	run_frame(&mono_frame);
	CHECK(fake.layers.panel);
	CHECK_NEAR(fake.layers.fade_alpha, 1.0, 1e-6);

	// mono -> stereo: the fade restarts and the recentre happens
	fake.time_ns += 5 * FRAME_NS;
	model_recentre();
	stereo_frame(0, NULL);
	CHECK_NEAR(fake.layers.fade_alpha, 1.0, 1e-6);
	CHECK_INT(vr_render_frame_info()->mode, VR_MODE_STEREO);
	CHECK(!render_vr_wide_cull()); // not recording between frames

	// ---- head yaw 0 (0.7 s later: the fade is over)
	printf("head yaw 0\n");
	fake.time_ns += 63 * FRAME_NS;
	stereo_frame(0, "yaw-000");
	CHECK_NEAR(fake.layers.fade_alpha, 0.0, 1e-6);
	glerrors("after a stereo frame");
	{
		const vr_layers_t *l = &fake.layers;
		CHECK_NEAR(l->hud_pose.position[0], 0, 1e-4);
		CHECK_NEAR(l->hud_pose.position[1], 1.6, 1e-4);
		CHECK_NEAR(l->hud_pose.position[2], -2.0, 1e-4);
		CHECK_NEAR(2 * atanf(l->hud_width / 2 / 2.0f), 40.0 * PI_F / 180.0, 1e-4);
		CHECK_NEAR(l->hud_width / l->hud_height, 16.0 / 9.0, 1e-4);
		// the eyes were rendered, and are submitted, with the poses the runtime gave
		CHECK_NEAR(l->views[0].pose.position[0], -IPD / 2, 1e-5);
		CHECK_NEAR(l->views[1].pose.position[0], IPD / 2, 1e-5);

		// The target icon through the head-centre camera onto the HUD's frustum
		double want = tan(10 * PI / 180) / tan(20 * PI / 180);
		printf("  reticle: ahead (%.3f, %.3f), 10 deg right x %.4f (expected %.4f), behind z %.2f\n",
			reticle_probe[0].x, reticle_probe[0].y, reticle_probe[1].x, want, reticle_probe[2].z);
		CHECK_NEAR(reticle_probe[0].x, 0, 1e-3);
		CHECK_NEAR(reticle_probe[0].y, 0, 1e-3);
		CHECK_NEAR(reticle_probe[1].x, want, 2e-3);
		CHECK(reticle_probe[2].z >= 1);
	}
	shot_t yaw0 = grab();
	uint8_t centre_eye0[4];
	memcpy(centre_eye0, px_at(yaw0.eye[0], EYE_W, EYE_H, EYE_W / 2, EYE_H / 2), 4);
	expect_marker(yaw0.eye[0], 0, &markers[0], "ahead", 6);
	expect_marker(yaw0.eye[1], 1, &markers[0], "ahead", 6);
	expect_absent(yaw0.eye[0], &markers[3], "behind");
	expect_sky(yaw0.eye[0], "yaw 0");
	check_hud_alpha(yaw0.hud);
	save_shot("yaw-000", &yaw0);
	free_shot(&yaw0);

	// ---- the stereo disparity has the right sign: a near object is further right in the left eye
	printf("disparity (symmetric fov)\n");
	fake.symmetric_fov = true;
	stereo_frame(0, NULL);
	{
		shot_t s = grab();
		blob_t l = find_colour(s.eye[0], EYE_W, EYE_H, 255, 128, 0, 12);
		blob_t r = find_colour(s.eye[1], EYE_W, EYE_H, 255, 128, 0, 12);
		double el_x, el_y, er_x, er_y;
		expect_pixel(0, markers[4].origin, &el_x, &el_y);
		expect_pixel(1, markers[4].origin, &er_x, &er_y);
		printf("  near marker: left eye x %.2f, right eye x %.2f, difference %.2f px (analytic %.2f)\n", l.cx, r.cx, l.cx - r.cx, el_x - er_x);
		CHECK(l.count > 100 && r.count > 100);
		CHECK(l.cx > r.cx + 4.0);
		CHECK_NEAR(l.cx - r.cx, el_x - er_x, 1.5);
		CHECK_NEAR(l.cy, r.cy, 0.5); // no vertical disparity
		free_shot(&s);
	}
	fake.symmetric_fov = false;

	// ---- a sprite faces the eye it is drawn for, whichever way the head turns
	printf("sprite facing (head turned 30 degrees left)\n");
	stereo_frame(30, NULL);
	{
		shot_t s = grab();
		for (int e = 0; e < 2; e++) {
			// additive magenta over sky or ground: red and blue saturated, green what is under it
			int min_x = 1 << 30, max_x = -1, min_y = 1 << 30, max_y = -1, count = 0;
			for (int y = 0; y < EYE_H; y++) {
				for (int x = 0; x < EYE_W; x++) {
					const uint8_t *p = px_at(s.eye[e], EYE_W, EYE_H, x, y);
					if (p[0] >= 250 && p[2] >= 250 && p[1] >= 55 && p[1] <= 170) {
						count++;
						if (x < min_x) min_x = x;
						if (x > max_x) max_x = x;
						if (y < min_y) min_y = y;
						if (y > max_y) max_y = y;
					}
				}
			}
			double w = max_x - min_x + 1, h = max_y - min_y + 1;
			printf("  eye%d sprite: %d px, %.0f x %.0f, aspect %.3f\n", e, count, w, h, w / h);
			CHECK(count > 400);
			CHECK(w / h > 0.94 && w / h < 1.06); // a billboard facing the game camera would be 0.87
		}
		free_shot(&s);
	}

	// ---- head yaw 90 to the right
	printf("head yaw 90 (to the right)\n");
	stereo_frame(-90, "yaw-090-right");
	{
		shot_t s = grab();
		expect_marker(s.eye[0], 0, &markers[1], "right", 6);
		expect_marker(s.eye[1], 1, &markers[1], "right", 6);
		expect_absent(s.eye[0], &markers[0], "ahead");
		expect_absent(s.eye[0], &markers[2], "left");
		expect_sky(s.eye[0], "yaw 90");
		check_hud_alpha(s.hud);
		save_shot("yaw-090-right", &s);
		free_shot(&s);
	}

	// ---- head yaw 180: the marker behind, and the sky, are drawn: distance-only culling
	printf("head yaw 180\n");
	stereo_frame(180, "yaw-180");
	{
		shot_t s = grab();
		expect_marker(s.eye[0], 0, &markers[3], "behind", 6);
		expect_marker(s.eye[1], 1, &markers[3], "behind", 6);
		expect_absent(s.eye[0], &markers[0], "ahead");
		expect_sky(s.eye[0], "yaw 180");
		expect_sky(s.eye[1], "yaw 180");
		check_hud_alpha(s.hud);
		save_shot("yaw-180", &s);
		free_shot(&s);
	}

	// ---- a recentre request at yaw 180 makes what the head faces the game's forward
	printf("recentre while facing 180\n");
	vr_render_request_recentre();
	model_recentre();
	stereo_frame(180, NULL);
	{
		shot_t s = grab();
		expect_marker(s.eye[0], 0, &markers[0], "ahead", 6);
		expect_marker(s.eye[1], 1, &markers[0], "ahead", 6);
		expect_absent(s.eye[0], &markers[3], "behind");
		free_shot(&s);
	}
	vr_render_request_recentre();
	fake.yaw = 0;
	model_recentre();
	stereo_frame(0, NULL);

	// ---- the sky stays at infinity when the head moves: re-translated to each eye
	printf("sky at infinity (head moves 1 m right)\n");
	{
		frame_opts_t sky_only = { .stereo = true, .sky = true, .sky_style = SKY_FACES };
		fake.symmetric_fov = true;
		int edge[2][2];
		for (int move = 0; move < 2; move++) {
			fake.pos[0] = move ? 1.0f : 0.0f;
			run_frame(&sky_only);
			shot_t s = grab();
			edge[move][0] = sky_edge(s.eye[0]);
			edge[move][1] = sky_edge(s.eye[1]);
			free_shot(&s);
		}
		printf("  +x face edge: head at 0 m: eyes %d/%d; head at 1 m: eyes %d/%d\n", edge[0][0], edge[0][1], edge[1][0], edge[1][1]);
		CHECK(edge[0][0] > 400 && edge[0][1] > 400);
		CHECK(abs(edge[1][0] - edge[0][0]) <= 2);
		CHECK(abs(edge[1][1] - edge[0][1]) <= 2);
		fake.pos[0] = 0;
		fake.symmetric_fov = false;
	}

	// ---- the HUD never rolls with the head
	printf("rolled head\n");
	fake.roll = 0.35f;
	stereo_frame(20, NULL);
	{
		const float *q = fake.layers.hud_pose.orientation;
		CHECK_NEAR(2 * (q[0] * q[1] + q[2] * q[3]), 0, 1e-4); // the quad's right vector stays horizontal
		CHECK_NEAR(vr_yaw_of(q), 20 * PI_F / 180, 1e-4);
	}
	fake.roll = 0;

	// ---- standby: the game runs, nothing is replayed or submitted
	printf("standby\n");
	uint32_t replayed = vr_render_frame_info()->frames_replayed;
	int acquires = fake.acquires;
	fake.renders = false;
	run_frame(&race_frame);
	CHECK(!fake.layers.projection && !fake.layers.hud && !fake.layers.panel);
	CHECK_INT(fake.acquires, acquires);
	CHECK_INT(vr_render_frame_info()->frames_replayed, 0); // the info is cleared for an unrendered frame
	(void)replayed;
	fake.renders = true;

	// ---- back to mono: the fade runs again
	run_frame(&mono_frame);
	CHECK(fake.layers.panel && !fake.layers.projection);
	CHECK_NEAR(fake.layers.fade_alpha, 1.0, 1e-6);

	// ---- every acquired image was released
	CHECK_INT(fake.acquires, fake.releases);
	glerrors("at the end of the stereo frames");
}


// GLSL smoothstep(edge0, edge1, x) as the vertex shader uses it (edge0 > edge1: far, near)
static float smoothstep_gl(float e0, float e1, float x) {
	float t = (x - e0) / (e1 - e0);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3 - 2 * t);
}

static void test_fade_is_measured_from_each_eye(void) {
	printf("distance fade from the eye's position\n");
	fake_reset();
	vr_render_config_t cfg = make_config(NULL, false);
	vr_render_attach(&cfg);
	cam_angles = vec3(0, 0, 0);
	frame_opts_t o = { .stereo = true, .sky = true, .sky_style = SKY_PLAIN, .far_cube = true };

	run_frame(&mono_frame);
	fake.time_ns += 70 * FRAME_NS;
	model_recentre();
	fake.symmetric_fov = true;

	// The cube's face is 55400 units ahead. Leaning 3 m forward (384 units) moves the
	// eyes closer, so it fades in: only a fade measured from the eye sees that.
	float blue[2][2];
	for (int lean = 0; lean < 2; lean++) {
		fake.pos[2] = lean ? -3.0f : 0.0f;
		run_frame(&o);
		shot_t s = grab();
		for (int e = 0; e < 2; e++) {
			double px, py;
			expect_pixel(e, vec3(far_cube_origin.x, far_cube_origin.y, far_cube_origin.z - FAR_CUBE_HALF), &px, &py);
			const uint8_t *p = px_at(s.eye[e], EYE_W, EYE_H, (int)(px + 0.5), (int)(py + 0.5));
			blue[lean][e] = p[2];
			// a white-blue sky behind: blue = 255 * (1 - alpha), with the cube's own blue at 0
			float half = (e == 0 ? -0.5f : 0.5f) * IPD;
			vec3_t eye = vec3(half * WORLD_SCALE, cam_pos.y, cam_pos.z + (lean ? 3.0f * WORLD_SCALE : 0));
			vec3_t face = vec3(far_cube_origin.x, far_cube_origin.y, far_cube_origin.z - FAR_CUBE_HALF);
			float d = vec3_len(vec3_sub(face, eye));
			float alpha = smoothstep_gl(RENDER_FADEOUT_FAR, RENDER_FADEOUT_NEAR, d);
			printf("  lean %d eye%d: distance %.0f, alpha %.3f, blue %d (expected %.1f)\n", lean, e, d, alpha, p[2], 255 * (1 - alpha));
			CHECK_NEAR(p[2], 255 * (1 - alpha), 4.0);
		}
		free_shot(&s);
	}
	CHECK(blue[0][0] - blue[1][0] >= 6);
	CHECK(blue[0][1] - blue[1][1] >= 6);
	fake.pos[2] = 0;
	vr_render_detach();
}

static void test_diag_yaw(void) {
	printf("diag_yaw turns the synthetic head after the recentre\n");
	fake_reset();
	vr_render_config_t cfg = make_config(NULL, false);
	cfg.settings.diag_yaw = -90.0f * PI_F / 180.0f; // to the right
	vr_render_attach(&cfg);
	cam_angles = vec3(0, 0, 0);
	rc_yaw = 0;
	memcpy(rc_origin, fake.pos, sizeof(rc_origin));

	run_frame(&mono_frame);
	fake.time_ns += 70 * FRAME_NS;
	fake.yaw = 0; // the runtime's head faces forward throughout
	run_frame(&race_frame);
	CHECK(fake.layers.projection);
	// the eyes are submitted with the turned poses they were rendered with
	CHECK_NEAR(vr_yaw_of(fake.layers.views[0].pose.orientation), -PI_F / 2, 1e-4);
	CHECK_NEAR(vr_yaw_of(fake.layers.views[1].pose.orientation), -PI_F / 2, 1e-4);

	shot_t s = grab();
	fake.yaw = -PI_F / 2; // for the expectation: the head the renderer used
	expect_marker(s.eye[0], 0, &markers[1], "right", 6);
	expect_marker(s.eye[1], 1, &markers[1], "right", 6);
	fake.yaw = 0;
	free_shot(&s);
	vr_render_detach();
}

// -----------------------------------------------------------------------------
// 4. force_mono, timing and the dumps

static void test_force_mono(void) {
	printf("force_mono keeps a race frame on the panel\n");
	fake_reset();
	vr_render_config_t cfg = make_config(NULL, false);
	cfg.settings.force_mono = true;
	vr_render_attach(&cfg);
	run_frame(&race_frame);
	CHECK(fake.layers.panel);
	CHECK(!fake.layers.projection && !fake.layers.hud);
	CHECK_INT(vr_render_frame_info()->mode, VR_MODE_MONO);
	vr_render_detach();
}

static void test_timing_and_dumps(void) {
	printf("timing, dumps\n");
	fake_reset();
	char dir[600];
	snprintf(dir, sizeof(dir), "%s/timing-dump", out_dir);
	make_dir(dir);
	vr_render_config_t cfg = make_config(dir, true);
	vr_render_attach(&cfg);
	vr_render_dump_request(3, dir);

	run_frame(&mono_frame); // 1
	run_frame(&race_frame); // 2: stereo, recentred
	fake.time_ns += 70 * FRAME_NS;
	run_frame(&race_frame); // 3: dumped
	const vr_render_timing_t *t = vr_render_timing();
	printf("  record %.3f ms, replay %.3f ms (eyes %.3f / %.3f), hud %.3f ms\n", t->record_ms, t->replay_ms, t->eye_ms[0], t->eye_ms[1], t->hud_ms);
	CHECK(t->record_ms > 0 && t->replay_ms > 0 && t->hud_ms > 0);
	CHECK(t->eye_ms[0] > 0 && t->eye_ms[1] > 0);

	// the dump is written one frame late: not yet
	char path[700];
	snprintf(path, sizeof(path), "%s/vr-eye0.bmp", dir);
	remove(path);
	// (the poll at the next frame's start writes whatever has finished)
	run_frame(&race_frame); // 4
	vr_render_dump_flush();

	const char *names[] = { "vr-eye0.bmp", "vr-eye1.bmp", "vr-hud.bmp", "vr-state.txt" };
	for (int i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		int size = file_size(path);
		printf("  %s: %d bytes\n", path, size);
		if (i == 0 || i == 1) {
			CHECK_INT(size, 54 + EYE_W * EYE_H * 3);
		}
		else if (i == 2) {
			CHECK(size > UI_W * UI_H * 4);
		}
		else {
			CHECK(size > 200);
		}
	}
	// the product's own BMP agrees with the image: the same pixel, the right way up
	{
		snprintf(path, sizeof(path), "%s/vr-eye0.bmp", dir);
		FILE *f = fopen(path, "rb");
		CHECK(f != NULL);
		if (f) {
			uint8_t *bmp = malloc(54 + EYE_W * EYE_H * 3);
			size_t n = fread(bmp, 1, 54 + EYE_W * EYE_H * 3, f);
			fclose(f);
			CHECK_INT(n, 54 + EYE_W * EYE_H * 3);
			CHECK(bmp[0] == 'B' && bmp[1] == 'M');
			// the dumped frame is the 3rd rendered one; the eye image has since been redrawn
			// by frame 4 with the same head, so the centre pixel is the same marker
			int off = bmp[10] | (bmp[11] << 8);
			int row = EYE_H - 1 - EYE_H / 2; // bottom-up rows
			const uint8_t *p = &bmp[off + ((size_t)row * EYE_W + EYE_W / 2) * 3];
			uint8_t *eye = read_texture(fake.surface[VR_SURFACE_EYE0].tex, EYE_W, EYE_H);
			const uint8_t *q = px_at(eye, EYE_W, EYE_H, EYE_W / 2, EYE_H / 2);
			printf("  centre pixel: bmp rgb(%d,%d,%d), image rgb(%d,%d,%d)\n", p[2], p[1], p[0], q[0], q[1], q[2]);
			CHECK(abs(p[2] - q[0]) <= 2 && abs(p[1] - q[1]) <= 2 && abs(p[0] - q[2]) <= 2);
			free(eye);
			free(bmp);
		}
	}
	snprintf(path, sizeof(path), "%s/vr-state.txt", dir);
	FILE *f = fopen(path, "r");
	CHECK(f != NULL);
	if (f) {
		char text[4096] = {0};
		size_t n = fread(text, 1, sizeof(text) - 1, f);
		fclose(f);
		text[n] = 0;
		CHECK(strstr(text, "mode stereo") != NULL);
		CHECK(strstr(text, "recentre yaw=") != NULL);
		CHECK(strstr(text, "hud pos=") != NULL);
	}

	// The [vr-perf] line, fed with these timings for 11 s of 90 Hz frames
	vr_perf_t perf;
	vr_perf_init(&perf);
	char line[256] = {0};
	int emitted = 0;
	for (int i = 0; i < 1000; i++) {
		if (vr_perf_frame(&perf, i / 90.0, 1000000000LL + (int64_t)i * FRAME_NS, FRAME_NS, t->record_ms, t->replay_ms, t->hud_ms, t->eye_ms, -1.0f, line, sizeof(line))) {
			emitted++;
			printf("  %s\n", line);
		}
	}
	CHECK_INT(emitted, 1);
	CHECK(strncmp(line, "[vr-perf] fps=", 14) == 0);
	CHECK(strstr(line, "record=") && strstr(line, "replay=") && strstr(line, "hud=") && strstr(line, "eye="));
	vr_render_detach();
}


// -----------------------------------------------------------------------------
// 5. GPU timing (timing_gpu): timestamp queries, read a few frames late, never waited for

static void test_gpu_timing(void) {
	printf("gpu timing\n");

	// off: nothing is created, nothing is reported, whatever is drawn
	fake_reset();
	vr_render_config_t cfg = make_config(NULL, false);
	vr_render_attach(&cfg);
	for (int i = 0; i < 8; i++) {
		run_frame(i % 2 ? &race_frame : &mono_frame);
		CHECK(!vr_render_gpu()->enabled);
		CHECK_INT(vr_render_gpu()->count, 0);
	}
	vr_render_detach();

	// on
	fake_reset();
	cfg = make_config(NULL, false);
	cfg.settings.timing_gpu = true;
	vr_render_attach(&cfg);
	int total = 0, stereo = 0, mono = 0, frames = 0, first_sample_frame = 0;
	float eye_max = 0, mono_max = 0, hud_max = 0;
	for (int i = 0; i < 24; i++) {
		// 6 stereo frames, then 6 mono frames, then stereo again
		run_frame((i / 6) % 2 == 0 ? &race_frame : &mono_frame);
		// llvmpipe is a slow GPU that has no vsync to pace the harness: let it finish each frame,
		// as the display does for the product, so what is under test is the ring and not the backlog
		glFinish();
		frames++;
		const vr_render_gpu_t *g = vr_render_gpu();
		if (frames == 1) {
			CHECK(g->enabled);
		}
		if (g->count > 0 && !first_sample_frame) {
			first_sample_frame = frames;
		}
		for (int k = 0; k < g->count; k++) {
			const vr_render_gpu_sample_t *sample = &g->samples[k];
			total++;
			if (sample->stereo) {
				stereo++;
				if (sample->eye_ms[0] > eye_max) eye_max = sample->eye_ms[0];
				if (sample->eye_ms[1] > eye_max) eye_max = sample->eye_ms[1];
				if (sample->hud_ms > hud_max) hud_max = sample->hud_ms;
				CHECK(sample->mono_ms == 0);
				CHECK(sample->eye_ms[0] >= 0 && sample->eye_ms[1] >= 0);
			}
			else {
				mono++;
				if (sample->mono_ms > mono_max) mono_max = sample->mono_ms;
				CHECK(sample->eye_ms[0] == 0 && sample->eye_ms[1] == 0 && sample->hud_ms == 0);
			}
			CHECK(sample->total_ms >= sample->mono_ms);
		}
	}
	printf("  %d samples (%d stereo, %d mono) from %d frames, first at frame %d; eye max %.3f ms, hud %.3f ms, mono %.3f ms; dropped %u\n",
		total, stereo, mono, frames, first_sample_frame, eye_max, hud_max, mono_max, vr_render_gpu()->dropped);
	CHECK(vr_render_gpu()->enabled);
	// read two to three frames late: nothing in the first two, then about one a frame
	CHECK(first_sample_frame >= 3 && first_sample_frame <= 5);
	CHECK(total >= frames - 5);
	CHECK(stereo > 0 && mono > 0);
	CHECK(eye_max > 0 && mono_max > 0 && hud_max > 0);
	glerrors("after gpu timing");
	vr_render_detach();
}


// -----------------------------------------------------------------------------

#include "vr_cockpit_harness.inc"

int main(int argc, char **argv) {
	if (argc > 1) {
		out_dir = argv[1];
	}
	make_dir(out_dir);

	SDL_setenv("SDL_VIDEODRIVER", "x11", 0);
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		printf("SDL_Init failed: %s\n", SDL_GetError());
		return 2;
	}
	// The VR build asks for a core 4.3 context (the runtime's minimum). Apple stops at 4.1, which
	// the renderer's GLSL 330 shaders run on, so a Mac developer's run falls back to it.
	for (int minor = 3; minor >= 1; minor -= 2) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
		window = SDL_CreateWindow("vr_render_harness", 0, 0, UI_W, UI_H, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
		if (!window) {
			printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
			return 2;
		}
		gl_context = SDL_GL_CreateContext(window);
		if (gl_context) {
			break;
		}
		printf("SDL_GL_CreateContext failed for 4.%d core: %s\n", minor, SDL_GetError());
		SDL_DestroyWindow(window);
		window = NULL;
	}
	if (!gl_context) {
		return 2;
	}
	SDL_GL_SetSwapInterval(0);
	printf("GL %s | %s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));

	// What vr_init() and system_init() do: attach the renderer, then render_init() with the window's size
	vr_render_config_t cfg = make_config(NULL, false);
	vr_render_attach(&cfg);
	render_init(vec2i(1280, 720));
	glGetError(); // glewInit leaves GL_INVALID_ENUM behind on a core context

	// The menus ask for a post effect and a low resolution; neither applies in VR
	render_set_post_effect(RENDER_POST_CRT);
	render_set_resolution(RENDER_RES_240P);
	render_set_screen_size(vec2i(1280, 720)); // the window: ignored
	CHECK_INT(render_size().x, UI_W);
	CHECK_INT(render_size().y, UI_H);

	fake_init();
	rgba_t magenta[16 * 16];
	for (int i = 0; i < 16 * 16; i++) {
		magenta[i] = rgba(255, 0, 255, 255);
	}
	sprite_texture = render_texture_create(16, 16, magenta);
	vr_render_detach();

	// WIPEOUT_VR_HARNESS_ONLY=cockpit runs just the cockpit tests (a developer's shortcut)
	const char *only = getenv("WIPEOUT_VR_HARNESS_ONLY");
	if (!only || strcmp(only, "cockpit") != 0) {
		test_mono_replay_is_the_flat_renderer();
		test_recording_draws_nothing();
		test_stereo_frames();
		vr_render_detach();
		test_diag_yaw();
		test_fade_is_measured_from_each_eye();
		test_force_mono();
		test_timing_and_dumps();
		test_gpu_timing();
	}
	test_cockpit_synthetic();
	data_root = getenv("WIPEOUT_VR_TEST_DATA");
	if (data_root && data_root[0]) {
		test_cockpit_real_ships();
	}
	else {
		printf("the real ships: skipped (set WIPEOUT_VR_TEST_DATA to a directory holding wipeout/...)\n");
	}

	printf("\noutput: %s\n", out_dir);
	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return SFVR_TEST_RESULT("vr_render_harness");
}
