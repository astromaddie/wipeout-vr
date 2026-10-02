// The stereo renderer's GL half (docs/VR_ARCHITECTURE.md sections 3 to 5).
//
// This file is not compiled on its own: render_gl.c includes it (WIPEOUT_VR
// builds only), after its static state and before render_init(), because the
// recording and the replay need that state: the atlas, the batch buffer, the
// game shader and the view matrices. The hooks in render_gl.c's own functions
// are one fenced `if (vrr.recording)` each.
//
// Recording: while the game runs, render_frame_prepare() to render_frame_end()
// appends commands to vrr.list (vr_cmd.h) and issues no draw GL. The flat
// renderer's state semantics are kept: render_set_view resets depth state and
// the model matrix, render_set_blend_mode ignores a repeat, view_mat and
// sprite_mat are updated for render_transform, and the blend and cull state
// persist across frames.
//
// Replay: the same list again, with the state set explicitly at the start of
// every pass, so a pass depends on nothing a previous pass left behind.
// Stereo: each eye (the 3D segment) and the HUD (the 2D segment, twice, over
// black and over white, then solved for alpha). Mono: everything once.

#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#include "vr/vr_render.h"
#include "vr/vr_cmd.h"
#include "vr/vr_log.h"

_Static_assert(sizeof(vr_vertex_t) == sizeof(vertex_t), "vr_vertex_t must match vertex_t");
_Static_assert(offsetof(vr_vertex_t, pos) == offsetof(vertex_t, pos), "vr_vertex_t must match vertex_t");
_Static_assert(offsetof(vr_vertex_t, uv) == offsetof(vertex_t, uv), "vr_vertex_t must match vertex_t");
_Static_assert(offsetof(vr_vertex_t, color) == offsetof(vertex_t, color), "vr_vertex_t must match vertex_t");

#define VR_PASSES_MAX 4 // eye, eye, HUD over black, HUD over white; mono uses one
#define VR_DUMP_SLOTS 4 // eye0, eye1, hud, panel
#define VR_CHECKED_MAX 64

static mat4_t render_setup_2d_projection_mat(vec2i_t size);
static mat4_t render_setup_3d_projection_mat(vec2i_t size);

typedef struct {
	GLuint pbo;
	GLsync fence;
	uint32_t width, height;
	int comp; // 3 or 4
	bool pending;
	char path[512];
} vr_dump_slot_t;

static struct {
	bool attached;
	vr_render_config_t cfg;
	vr_stage_t stage;
	vr_cmdlist_t list;

	// The frame being recorded
	bool recording;
	bool renders; // the provider wants this frame drawn
	bool views_ok; // and the eye poses are valid, so it can be stereo
	bool stereo;
	bool in_2d;
	bool sky;
	const vr_frame_t *frame;
	vr_view_t views[2]; // the frame's views after diag_yaw: what is rendered and submitted
	vr_pose_t head;
	int64_t now_ns;
	bool cull; // GL_CULL_FACE as the flat renderer would have it; persists across frames
	int32_t cur_cam;
	float eye_proj[2][16];
	vr_pose_t hud_pose;
	float hud_width, hud_height; // metres
	float hud_proj[16]; // the HUD quad's frustum seen from the head centre
	bool hud_mode_logged;

	// GL objects, made at the first replay
	bool gl_ready;
	GLuint fbo;
	GLuint depth_rb[3]; // eye0, eye1, panel
	uint32_t depth_w[3], depth_h[3];
	GLuint hud_tex[2]; // over black, over white
	uint32_t hud_tex_w, hud_tex_h;
	GLuint resolve_program, resolve_vao;
	uint32_t checked_images[VR_CHECKED_MAX];
	int checked_count;
	vr_vertex_t *sprite_scratch;
	uint32_t sprite_scratch_cap;
	uint32_t passes_this_frame;
	uint32_t draw_calls;

	// Diagnostics
	uint32_t frames_replayed;
	int dump_frame;
	char dump_dir[400];
	bool dump_this_frame;
	vr_dump_slot_t dump_slots[VR_DUMP_SLOTS];
	uint64_t rec_ns;
	vr_render_timing_t timing;
	vr_render_frame_info_t info;
} vrr;


// -----------------------------------------------------------------------------
// Helpers

static uint64_t vr_clock_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Recording is timed only under vr.timing; the clock reads are the cost.
#define VR_TIME_BEGIN() uint64_t vr_t0 = vrr.cfg.settings.timing ? vr_clock_ns() : 0
#define VR_TIME_END() do { if (vr_t0) { vrr.rec_ns += vr_clock_ns() - vr_t0; } } while (0)

static uint8_t vr_seg_flags(void) {
	return vrr.in_2d ? VR_CMD_FLAG_2D : 0;
}

bool vr_render_active(void) {
	return vrr.attached;
}

const vr_render_timing_t *vr_render_timing(void) {
	return &vrr.timing;
}

const vr_render_frame_info_t *vr_render_frame_info(void) {
	return &vrr.info;
}

void vr_render_request_recentre(void) {
	vr_stage_request_recentre(&vrr.stage);
}

void vr_render_dump_request(int frame_number, const char *dir) {
	vrr.dump_frame = frame_number;
	snprintf(vrr.dump_dir, sizeof(vrr.dump_dir), "%s", dir ? dir : "");
}


// -----------------------------------------------------------------------------
// Dumps: read back one frame late, never stall

static void vr_mkdirs(const char *path) {
	char buf[512];
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

static void vr_dump_write(vr_dump_slot_t *s) {
	glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
	const uint8_t *pixels = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
	if (pixels) {
		uint8_t *out = malloc((size_t)s->width * s->height * s->comp);
		if (out) {
			for (uint32_t y = 0; y < s->height; y++) {
				const uint8_t *src = pixels + (size_t)(s->height - 1 - y) * s->width * 4; // GL is bottom-up
				uint8_t *dst = out + (size_t)y * s->width * s->comp;
				for (uint32_t x = 0; x < s->width; x++) {
					for (int c = 0; c < s->comp; c++) {
						dst[x * s->comp + c] = src[x * 4 + c];
					}
				}
			}
			if (stbi_write_bmp(s->path, s->width, s->height, s->comp, out)) {
				vr_logf("dump: wrote %s (%ux%u)", s->path, s->width, s->height);
			}
			else {
				vr_logf("dump: could not write %s", s->path);
			}
			free(out);
		}
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glDeleteSync(s->fence);
	s->fence = 0;
	s->pending = false;
}

// Writes the dumps whose copy has finished. With `block` it waits for the rest.
static void vr_dump_poll(bool block) {
	for (int i = 0; i < VR_DUMP_SLOTS; i++) {
		vr_dump_slot_t *s = &vrr.dump_slots[i];
		if (!s->pending) {
			continue;
		}
		GLenum r = glClientWaitSync(s->fence, block ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, block ? 1000000000ull : 0);
		if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED || block) {
			vr_dump_write(s);
		}
	}
}

// Starts the copy of an image into a PBO; the file is written by a later poll.
static void vr_dump_queue(int slot, const char *name, GLuint image, uint32_t w, uint32_t h, int comp) {
	vr_dump_slot_t *s = &vrr.dump_slots[slot];
	if (s->pending) {
		vr_dump_write(s); // the previous dump never got collected; do not leak it
	}
	if (!s->pbo) {
		glGenBuffers(1, &s->pbo);
	}
	snprintf(s->path, sizeof(s->path), "%s/%s", vrr.dump_dir, name);
	s->width = w;
	s->height = h;
	s->comp = comp;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, vrr.fbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image, 0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
	glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, NULL, GL_STREAM_READ);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	s->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	s->pending = true;
}

static void vr_dump_state_log(void) {
	char path[512];
	snprintf(path, sizeof(path), "%s/vr-state.txt", vrr.dump_dir);
	FILE *f = fopen(path, "w");
	if (!f) {
		vr_logf("dump: could not write %s", path);
		return;
	}
	const vr_render_settings_t *st = &vrr.cfg.settings;
	fprintf(f, "frame %u (dump_frame %d)\n", vrr.frames_replayed, vrr.dump_frame);
	fprintf(f, "mode %s\n", vrr.stage.mode == VR_MODE_STEREO ? "stereo" : "mono");
	fprintf(f, "predicted_display_time_ns %lld\n", (long long)vrr.now_ns);
	fprintf(f, "settings world_scale=%.3f head_translation=%.3f follow_vehicle_rotation=%d horizon_lock=%d hud_distance=%.3f hud_scale=%.1f force_mono=%d diag_yaw_deg=%.2f\n",
		st->rig.world_scale, st->rig.head_translation, st->rig.follow_roll, st->rig.horizon_lock,
		st->hud_distance, st->hud_scale, st->force_mono, st->diag_yaw * 180.0 / M_PI);
	fprintf(f, "recentre yaw=%.5f origin=%.4f,%.4f,%.4f applied=%u switches=%u\n",
		vrr.stage.recentre.yaw, vrr.stage.recentre.origin[0], vrr.stage.recentre.origin[1], vrr.stage.recentre.origin[2],
		vrr.stage.recentres_applied, vrr.stage.mode_switches);
	fprintf(f, "head pos=%.4f,%.4f,%.4f quat=%.5f,%.5f,%.5f,%.5f yaw=%.5f\n",
		vrr.head.position[0], vrr.head.position[1], vrr.head.position[2],
		vrr.head.orientation[0], vrr.head.orientation[1], vrr.head.orientation[2], vrr.head.orientation[3],
		vr_yaw_of(vrr.head.orientation));
	for (int e = 0; e < 2; e++) {
		const vr_view_t *v = &vrr.views[e];
		fprintf(f, "eye%d pose pos=%.4f,%.4f,%.4f quat=%.5f,%.5f,%.5f,%.5f fov=%.4f,%.4f,%.4f,%.4f\n", e,
			v->pose.position[0], v->pose.position[1], v->pose.position[2],
			v->pose.orientation[0], v->pose.orientation[1], v->pose.orientation[2], v->pose.orientation[3],
			v->fov[0], v->fov[1], v->fov[2], v->fov[3]);
	}
	if (vrr.stage.mode == VR_MODE_STEREO) {
		for (uint32_t i = 0; i < vrr.list.cam_len; i++) {
			const vr_camset_t *c = &vrr.list.cams[i];
			fprintf(f, "view %u game_position=%.2f,%.2f,%.2f\n", i, c->game_position[0], c->game_position[1], c->game_position[2]);
			for (int e = 0; e < 2; e++) {
				fprintf(f, "  eye%d game position=%.3f,%.3f,%.3f\n", e, c->eye[e].position[0], c->eye[e].position[1], c->eye[e].position[2]);
			}
		}
		fprintf(f, "hud pos=%.4f,%.4f,%.4f quat=%.5f,%.5f,%.5f,%.5f size=%.4fx%.4f m (distance %.2f, scale %.0f%%)\n",
			vrr.hud_pose.position[0], vrr.hud_pose.position[1], vrr.hud_pose.position[2],
			vrr.hud_pose.orientation[0], vrr.hud_pose.orientation[1], vrr.hud_pose.orientation[2], vrr.hud_pose.orientation[3],
			vrr.hud_width, vrr.hud_height, st->hud_distance, st->hud_scale);
	}
	fprintf(f, "fade_alpha %.3f\n", vrr.info.fade_alpha);
	fprintf(f, "commands %u triangles %u sprites %u draw_calls %u\n", vrr.info.commands, vrr.info.triangles, vrr.info.sprites, vrr.info.draw_calls);
	fprintf(f, "timing record_ms=%.3f replay_ms=%.3f hud_ms=%.3f eye_ms=%.3f/%.3f\n",
		vrr.timing.record_ms, vrr.timing.replay_ms, vrr.timing.hud_ms, vrr.timing.eye_ms[0], vrr.timing.eye_ms[1]);
	fclose(f);
}

void vr_render_dump_flush(void) {
	if (vrr.gl_ready) {
		vr_dump_poll(true);
	}
}


// -----------------------------------------------------------------------------
// Attach

void vr_render_attach(const vr_render_config_t *config) {
	memset(&vrr, 0, sizeof(vrr));
	vrr.cfg = *config;
	vrr.attached = true;
	vrr.cull = true; // render_init() enables GL_CULL_FACE
	vrr.dump_frame = config->settings.dump_frame;
	vr_render_dump_request(config->settings.dump_frame, config->dump_dir);
	vr_stage_init(&vrr.stage);
	vr_cmdlist_init(&vrr.list);
	vr_logf("renderer attached: 2D layout %dx%d, world_scale %.1f, hud %.2f m at %.0f%%",
		config->settings.ui_width, config->settings.ui_height,
		config->settings.rig.world_scale, config->settings.hud_distance, config->settings.hud_scale);
	if (backbuffer) {
		// render_init() already ran: take the VR layout size now
		render_set_screen_size(vec2i(config->settings.ui_width, config->settings.ui_height));
	}
}

void vr_render_detach(void) {
	if (!vrr.attached) {
		return;
	}
	vr_render_dump_flush();
	vrr.attached = false;
	vrr.recording = false;
	vr_cmdlist_free(&vrr.list);
	free(vrr.sprite_scratch);
	vrr.sprite_scratch = NULL;
	vrr.sprite_scratch_cap = 0;
}


// -----------------------------------------------------------------------------
// Recording

static bool vr_begin_frame(void) {
	if (!vrr.attached) {
		return false;
	}
	if (vrr.gl_ready) {
		vr_dump_poll(false);
	}

	bool renders = false;
	vrr.frame = vrr.cfg.provider.frame(vrr.cfg.provider.user, &renders);
	vrr.renders = renders && vrr.frame;
	vrr.views_ok = vrr.renders && vrr.frame->views_valid;
	vrr.now_ns = vrr.frame ? vrr.frame->predicted_display_time : 0;
	vrr.recording = true;
	vrr.stereo = false;
	vrr.in_2d = false;
	vrr.sky = false;
	vrr.cur_cam = -1;
	vrr.rec_ns = 0;
	vrr.draw_calls = 0;
	vrr.passes_this_frame = 0;
	vrr.timing = (vr_render_timing_t){0};

	if (vrr.views_ok) {
		memcpy(vrr.views, vrr.frame->views, sizeof(vrr.views));
		if (vrr.cfg.settings.diag_yaw != 0) {
			vr_views_apply_yaw(vrr.views, vrr.cfg.settings.diag_yaw);
		}
		vr_head_pose(vrr.views, &vrr.head);
	}

	vr_cmdlist_reset(&vrr.list, blend_mode, vrr.cull);
	running_stats.num_tris = 0;
	running_stats.num_draw_calls = 0;
	return true;
}

static void vr_rec_view(vec3_t pos, vec3_t angles) {
	VR_TIME_BEGIN();

	// The state the flat renderer keeps: render_transform() and the sprites use it
	view_mat = mat4_identity();
	mat4_set_translation(&view_mat, vec3(0, 0, 0));
	mat4_set_roll_pitch_yaw(&view_mat, vec3(angles.x, -angles.y + M_PI, angles.z + M_PI));
	mat4_translate(&view_mat, vec3_inv(pos));
	mat4_set_yaw_pitch_roll(&sprite_mat, vec3(-angles.x, angles.y - M_PI, 0));

	vr_camset_t cs;
	memset(&cs, 0, sizeof(cs));
	cs.game_position[0] = pos.x;
	cs.game_position[1] = pos.y;
	cs.game_position[2] = pos.z;
	memcpy(cs.mono.position, cs.game_position, sizeof(cs.mono.position));
	memcpy(cs.mono.view, view_mat.m, sizeof(cs.mono.view));
	for (int c = 0; c < 3; c++) {
		for (int r = 0; r < 3; r++) {
			cs.mono.sprite[c * 3 + r] = sprite_mat.cols[c][r];
		}
	}

	if (vrr.stereo) {
		const vr_render_settings_t *st = &vrr.cfg.settings;
		vr_game_camera_t gc = { .position = {pos.x, pos.y, pos.z}, .angles = {angles.x, angles.y, angles.z} };
		for (int e = 0; e < 2; e++) {
			vr_cam_from_pose(&gc, &st->rig, &vrr.stage.recentre, &vrr.head, &vrr.views[e].pose, &cs.eye[e]);
		}
		// The head centre without its roll: the HUD quad's own point of view
		vr_pose_t level;
		vr_pose_level(&vrr.head, &level);
		vr_cam_from_pose(&gc, &st->rig, &vrr.stage.recentre, &vrr.head, &level, &cs.hud);
		cs.stereo = true;
	}

	vrr.cur_cam = vr_cmdlist_push_cam(&vrr.list, &cs);
	vr_cmd_t *c = vr_cmdlist_push(&vrr.list, VR_CMD_VIEW, 0);
	c->u.cam = vrr.cur_cam;
	vrr.in_2d = false;
	VR_TIME_END();
}

static void vr_rec_view_2d(void) {
	vr_cmdlist_push(&vrr.list, VR_CMD_VIEW_2D, 0);
	vrr.in_2d = true;
}

static void vr_rec_model_mat(const mat4_t *m) {
	VR_TIME_BEGIN();
	vr_cmd_t *c = vr_cmdlist_push(&vrr.list, VR_CMD_MODEL_MAT, vrr.sky ? VR_CMD_FLAG_SKY : 0);
	memcpy(c->u.model, m->m, sizeof(c->u.model));
	VR_TIME_END();
}

static void vr_rec_bool(vr_cmd_type_t type, bool on) {
	vr_cmd_t *c = vr_cmdlist_push(&vrr.list, type, 0);
	c->u.on = on;
}

static void vr_rec_tris(const tris_t *tris) {
	VR_TIME_BEGIN();
	vr_cmdlist_push_tris(&vrr.list, (const vr_vertex_t *)tris->vertices, vr_seg_flags());
	VR_TIME_END();
}

static void vr_rec_sprite(vec3_t pos, vec2i_t size, rgba_t color, uint16_t texture) {
	VR_TIME_BEGIN();
	vr_cmd_t *c = vr_cmdlist_push(&vrr.list, VR_CMD_SPRITE, vr_seg_flags());
	c->u.sprite.pos[0] = pos.x;
	c->u.sprite.pos[1] = pos.y;
	c->u.sprite.pos[2] = pos.z;
	c->u.sprite.size[0] = size.x;
	c->u.sprite.size[1] = size.y;
	c->u.sprite.color[0] = color.r;
	c->u.sprite.color[1] = color.g;
	c->u.sprite.color[2] = color.b;
	c->u.sprite.color[3] = color.a;
	c->u.sprite.texture = texture;
	VR_TIME_END();
}

// render_transform() in a stereo frame: through the head-centre camera onto the
// HUD quad's frustum, so the reticle lands where its target is seen from the head.
static vec3_t vr_rec_transform(vec3_t pos) {
	const vr_camset_t *cs = &vrr.list.cams[vrr.cur_cam];
	float point[3] = {pos.x, pos.y, pos.z}, ndc[3];
	vr_project(cs->hud.view, vrr.hud_proj, point, ndc);
	return vec3(ndc[0], ndc[1], ndc[2]);
}

// The game marks a frame that shows the race camera: stereo when the head can be
// tracked and vr.force_mono is off. Called before render_set_view with the race camera.
void render_vr_race_view(bool stereo) {
	if (!vrr.recording || vrr.stereo) {
		return;
	}
	const vr_render_settings_t *st = &vrr.cfg.settings;
	if (!stereo || !vrr.views_ok || st->force_mono) {
		return;
	}

	vrr.stereo = true;
	vr_stage_set_mode(&vrr.stage, VR_MODE_STEREO, vrr.now_ns);
	// A recentre takes the head the runtime reports, so diag_yaw's turn on top of it stays visible
	vr_stage_apply_recentre(&vrr.stage, vrr.frame->views);

	for (int e = 0; e < 2; e++) {
		vr_projection_fov(vrr.views[e].fov, NEAR_PLANE, FAR_PLANE, vrr.eye_proj[e]);
	}

	if (st->hud_mode != 0 && !vrr.hud_mode_logged) {
		vrr.hud_mode_logged = true;
		vr_logf("hud_mode far and leash are not built yet; using near");
	}
	float aspect = (float)st->ui_width / (float)st->ui_height;
	vr_hud_pose(&vrr.head, st->hud_distance, &vrr.hud_pose);
	vr_hud_size(st->hud_distance, st->hud_scale, aspect, &vrr.hud_width, &vrr.hud_height);
	float tx = vrr.hud_width * 0.5f / st->hud_distance;
	float ty = vrr.hud_height * 0.5f / st->hud_distance;
	vr_projection_tan(-tx, tx, ty, -ty, NEAR_PLANE, FAR_PLANE, vrr.hud_proj);
}

// Distance-only culling (track_draw, scene_draw): everything within the fade
// range is drawn, so a turned head sees the scenery beside and behind it.
bool render_vr_wide_cull(void) {
	return vrr.recording && vrr.stereo;
}

// The sky object follows the camera, so each eye re-translates it to itself.
void render_vr_sky(bool on) {
	vrr.sky = on;
}


// -----------------------------------------------------------------------------
// Replay

static const char * const SHADER_RESOLVE_VS = SHADER_SOURCE_330(
	out vec2 v_uv;
	void main(void) {
		vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
		v_uv = p;
		gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
	}
);

// The HUD over black is premultiplied colour; over white it is that plus what
// the HUD left uncovered. Their difference is the coverage.
static const char * const SHADER_RESOLVE_FS = SHADER_SOURCE_330(
	in vec2 v_uv;
	uniform sampler2D over_black;
	uniform sampler2D over_white;
	out vec4 frag_color;
	void main(void) {
		vec3 k = texture(over_black, v_uv).rgb;
		vec3 w = texture(over_white, v_uv).rgb;
		float a = clamp(1.0 - dot(w - k, vec3(1.0 / 3.0)), 0.0, 1.0);
		vec3 c = a > 0.002 ? clamp(k / a, 0.0, 1.0) : vec3(0.0);
		frag_color = vec4(c, a);
	}
);

static void vr_gl_make(void) {
	glGenFramebuffers(1, &vrr.fbo);
	glGenRenderbuffers(3, vrr.depth_rb);
	glGenTextures(2, vrr.hud_tex);

	vrr.resolve_program = create_program(SHADER_RESOLVE_VS, SHADER_RESOLVE_FS);
	glUseProgram(vrr.resolve_program);
	glUniform1i(glGetUniformLocation(vrr.resolve_program, "over_black"), 0);
	glUniform1i(glGetUniformLocation(vrr.resolve_program, "over_white"), 1);
	glGenVertexArrays(1, &vrr.resolve_vao);
	vrr.gl_ready = true;
}

// Mipmaps for the atlas, and the arena into the batch buffer. Sprites get a
// region per pass after the arena, so a pass never overwrites what an earlier
// one is still reading.
static void vr_gl_upload(void) {
	if (texture_mipmap_is_dirty) {
		glBindTexture(GL_TEXTURE_2D, atlas_texture);
		glGenerateMipmap(GL_TEXTURE_2D);
		texture_mipmap_is_dirty = false;
	}
	size_t arena = (size_t)vrr.list.vert_len * sizeof(vr_vertex_t);
	size_t sprites = (size_t)vrr.list.sprite_count * 6 * sizeof(vr_vertex_t) * VR_PASSES_MAX;
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(arena + sprites + sizeof(vr_vertex_t)), NULL, GL_STREAM_DRAW);
	if (arena) {
		glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)arena, vrr.list.verts);
	}
}

static void vr_ensure_depth(int slot, uint32_t w, uint32_t h) {
	if (vrr.depth_w[slot] == w && vrr.depth_h[slot] == h) {
		return;
	}
	glBindRenderbuffer(GL_RENDERBUFFER, vrr.depth_rb[slot]);
	glRenderbufferStorage(GL_RENDERBUFFER, RENDER_DEPTH_BUFFER_INTERNAL_FORMAT, w, h);
	vrr.depth_w[slot] = w;
	vrr.depth_h[slot] = h;
}

static void vr_ensure_hud_textures(uint32_t w, uint32_t h) {
	if (vrr.hud_tex_w == w && vrr.hud_tex_h == h) {
		return;
	}
	for (int i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, vrr.hud_tex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	vrr.hud_tex_w = w;
	vrr.hud_tex_h = h;
}

// One completeness check per image: the swapchain rotates through a few.
static void vr_check_target(GLuint image) {
	for (int i = 0; i < vrr.checked_count; i++) {
		if (vrr.checked_images[i] == image) {
			return;
		}
	}
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		vr_logf("WARNING: framebuffer for image %u is incomplete (0x%x)", image, status);
	}
	if (vrr.checked_count < VR_CHECKED_MAX) {
		vrr.checked_images[vrr.checked_count++] = image;
	}
}

typedef enum {
	VR_PASS_EYE,
	VR_PASS_HUD,
	VR_PASS_MONO
} vr_pass_kind_t;

typedef struct {
	vr_pass_kind_t kind;
	int eye;
	GLuint image;
	uint32_t width, height;
	GLuint depth; // 0 for none
	float clear[4];
	uint8_t segments; // bit 0: the 3D segment, bit 1: the 2D segment
} vr_pass_t;

static void vr_expand_sprite(const vr_cmd_t *c, const float sprite[9], vr_vertex_t out[6]) {
	// render_push_sprite(), with the sprite matrix of the camera being drawn from
	mat4_t sm = mat4_identity();
	for (int col = 0; col < 3; col++) {
		for (int r = 0; r < 3; r++) {
			sm.cols[col][r] = sprite[col * 3 + r];
		}
	}
	vec3_t pos = vec3(c->u.sprite.pos[0], c->u.sprite.pos[1], c->u.sprite.pos[2]);
	vec2i_t size = vec2i(c->u.sprite.size[0], c->u.sprite.size[1]);
	vec3_t p1 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5, -size.y * 0.5, 0), &sm));
	vec3_t p2 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5, -size.y * 0.5, 0), &sm));
	vec3_t p3 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5,  size.y * 0.5, 0), &sm));
	vec3_t p4 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5,  size.y * 0.5, 0), &sm));

	const render_texture_t *t = &textures[c->u.sprite.texture];
	vec3_t p[6] = {p1, p2, p3, p3, p2, p4};
	vec2_t uv[6] = {
		{0, 0}, {(float)t->size.x, 0}, {0, (float)t->size.y},
		{0, (float)t->size.y}, {(float)t->size.x, 0}, {(float)t->size.x, (float)t->size.y}
	};
	for (int i = 0; i < 6; i++) {
		out[i].pos[0] = p[i].x;
		out[i].pos[1] = p[i].y;
		out[i].pos[2] = p[i].z;
		out[i].uv[0] = uv[i].x + t->offset.x;
		out[i].uv[1] = uv[i].y + t->offset.y;
		memcpy(out[i].color, &c->u.sprite.color, 4);
	}
}

// The camera and projection a pass draws a VIEW command with
static void vr_pass_cam(const vr_pass_t *p, const vr_camset_t *cs, const vr_cam_t **cam, const float **proj) {
	if (p->kind == VR_PASS_EYE && cs->stereo) {
		*cam = &cs->eye[p->eye];
		*proj = vrr.eye_proj[p->eye];
	}
	else {
		*cam = &cs->mono;
		*proj = projection_mat_3d.m;
	}
}

static void vr_pass(const vr_pass_t *p) {
	vr_cmdlist_t *l = &vrr.list;
	const uint32_t arena_tris = l->vert_len / 3;
	const uint32_t pass_index = vrr.passes_this_frame++;

	// Target
	glBindFramebuffer(GL_FRAMEBUFFER, vrr.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->image, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, p->depth);
	vr_check_target(p->image);
	glViewport(0, 0, p->width, p->height);

	// The state a pass starts in, set in full: nothing is inherited
	glDisable(GL_FRAMEBUFFER_SRGB); // the game's colours are gamma-encoded already (lesson 8)
	glDisable(GL_SCISSOR_TEST);
	use_program(prg_game);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, atlas_texture);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(true);
	glDisable(GL_POLYGON_OFFSET_FILL);
	if (l->initial_cull) {
		glEnable(GL_CULL_FACE);
	}
	else {
		glDisable(GL_CULL_FACE);
	}
	if (l->initial_blend == RENDER_BLEND_LIGHTER) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	}
	else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glUniform2f(prg_game->uniform.screen, 0, 0);
	glUniformMatrix4fv(prg_game->uniform.model, 1, false, mat4_identity().m);
	glClearColor(p->clear[0], p->clear[1], p->clear[2], p->clear[3]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Sprites for this pass, each facing the camera it is drawn from
	const bool need_sprites = l->sprite_count > 0;
	if (need_sprites) {
		uint32_t needed = l->sprite_count * 6;
		if (vrr.sprite_scratch_cap < needed) {
			vrr.sprite_scratch = realloc(vrr.sprite_scratch, (size_t)needed * sizeof(vr_vertex_t));
			vrr.sprite_scratch_cap = needed;
		}
		uint32_t n = 0;
		const vr_camset_t *cs = NULL;
		for (uint32_t i = 0; i < l->cmd_len; i++) {
			const vr_cmd_t *c = &l->cmds[i];
			if (c->type == VR_CMD_VIEW) {
				cs = &l->cams[c->u.cam];
			}
			else if (c->type == VR_CMD_SPRITE) {
				uint8_t seg = (c->flags & VR_CMD_FLAG_2D) ? 2 : 1;
				if (!(p->segments & seg)) {
					continue;
				}
				static const float identity3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
				const float *s = identity3;
				if (cs) {
					const vr_cam_t *cam;
					const float *proj;
					vr_pass_cam(p, cs, &cam, &proj);
					s = cam->sprite;
				}
				vr_expand_sprite(c, s, &vrr.sprite_scratch[n * 6]);
				n++;
			}
		}
		if (n) {
			size_t offset = ((size_t)arena_tris * 3 + (size_t)pass_index * l->sprite_count * 6) * sizeof(vr_vertex_t);
			glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)offset, (GLsizeiptr)((size_t)n * 6 * sizeof(vr_vertex_t)), vrr.sprite_scratch);
		}
	}

	// The commands
	const vr_camset_t *cs = NULL;
	const float *eye_position = NULL;
	uint32_t sprite_n = 0;
	uint32_t pending_first = 0, pending_count = 0;
	uint32_t draws = 0;

	#define VR_FLUSH_DRAW() do { \
		if (pending_count) { \
			glDrawArrays(GL_TRIANGLES, pending_first * 3, pending_count * 3); \
			draws++; \
			pending_count = 0; \
		} \
	} while (0)

	#define VR_ADD_DRAW(FIRST, COUNT) do { \
		if (pending_count && pending_first + pending_count == (FIRST)) { \
			pending_count += (COUNT); \
		} \
		else { \
			VR_FLUSH_DRAW(); \
			pending_first = (FIRST); \
			pending_count = (COUNT); \
		} \
	} while (0)

	for (uint32_t i = 0; i < l->cmd_len; i++) {
		const vr_cmd_t *c = &l->cmds[i];
		switch ((vr_cmd_type_t)c->type) {
			case VR_CMD_TRIS:
				if (p->segments & ((c->flags & VR_CMD_FLAG_2D) ? 2 : 1)) {
					VR_ADD_DRAW(c->u.tris.first, c->u.tris.count);
				}
				break;

			case VR_CMD_SPRITE:
				if (p->segments & ((c->flags & VR_CMD_FLAG_2D) ? 2 : 1)) {
					uint32_t first = arena_tris + pass_index * l->sprite_count * 2 + sprite_n * 2;
					sprite_n++;
					VR_ADD_DRAW(first, 2);
				}
				break;

			case VR_CMD_VIEW: {
				VR_FLUSH_DRAW();
				cs = &l->cams[c->u.cam];
				const vr_cam_t *cam;
				const float *proj;
				vr_pass_cam(p, cs, &cam, &proj);
				eye_position = (p->kind == VR_PASS_EYE && cs->stereo) ? cam->position : NULL;
				glDepthMask(true);
				glEnable(GL_DEPTH_TEST);
				glUniformMatrix4fv(prg_game->uniform.model, 1, false, mat4_identity().m);
				glUniformMatrix4fv(prg_game->uniform.view, 1, false, cam->view);
				glUniformMatrix4fv(prg_game->uniform.projection, 1, false, proj);
				// the fade is measured from the camera being drawn from
				glUniform3f(prg_game->uniform.camera_pos, cam->position[0], cam->position[1], cam->position[2]);
				glUniform2f(prg_game->uniform.fade, RENDER_FADEOUT_NEAR, RENDER_FADEOUT_FAR);
				break;
			}

			case VR_CMD_VIEW_2D:
				VR_FLUSH_DRAW();
				glDisable(GL_DEPTH_TEST);
				glDepthMask(false);
				glUniformMatrix4fv(prg_game->uniform.model, 1, false, mat4_identity().m);
				glUniform3f(prg_game->uniform.camera_pos, 0, 0, 0);
				glUniformMatrix4fv(prg_game->uniform.view, 1, false, mat4_identity().m);
				glUniformMatrix4fv(prg_game->uniform.projection, 1, false, projection_mat_2d.m);
				break;

			case VR_CMD_MODEL_MAT: {
				VR_FLUSH_DRAW();
				float m[16];
				memcpy(m, c->u.model, sizeof(m));
				if ((c->flags & VR_CMD_FLAG_SKY) && eye_position && cs) {
					// the sky sits on the camera: move it from the game camera to this eye
					for (int k = 0; k < 3; k++) {
						m[12 + k] += eye_position[k] - cs->game_position[k];
					}
				}
				glUniformMatrix4fv(prg_game->uniform.model, 1, false, m);
				break;
			}

			case VR_CMD_DEPTH_WRITE:
				VR_FLUSH_DRAW();
				glDepthMask(c->u.on);
				break;

			case VR_CMD_DEPTH_TEST:
				VR_FLUSH_DRAW();
				if (c->u.on) {
					glEnable(GL_DEPTH_TEST);
				}
				else {
					glDisable(GL_DEPTH_TEST);
				}
				break;

			case VR_CMD_DEPTH_OFFSET:
				VR_FLUSH_DRAW();
				if (c->u.offset == 0) {
					glDisable(GL_POLYGON_OFFSET_FILL);
				}
				else {
					glEnable(GL_POLYGON_OFFSET_FILL);
					glPolygonOffset(c->u.offset, 1.0);
				}
				break;

			case VR_CMD_BLEND:
				VR_FLUSH_DRAW();
				if (c->u.blend == RENDER_BLEND_NORMAL) {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				}
				else if (c->u.blend == RENDER_BLEND_LIGHTER) {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				}
				break;

			case VR_CMD_CULL:
				VR_FLUSH_DRAW();
				if (c->u.on) {
					glEnable(GL_CULL_FACE);
				}
				else {
					glDisable(GL_CULL_FACE);
				}
				break;

			case VR_CMD_SCREEN_POS:
				VR_FLUSH_DRAW();
				// Screen shake is ignored in the eyes and the HUD; the mono panel keeps
				// it (the main menu uses it to place its models)
				if (p->kind == VR_PASS_MONO) {
					glUniform2f(prg_game->uniform.screen, c->u.screen[0], -c->u.screen[1]);
				}
				break;
		}
	}
	VR_FLUSH_DRAW();

	#undef VR_ADD_DRAW
	#undef VR_FLUSH_DRAW

	vrr.draw_calls += draws;
}

static void vr_replay_eyes(vr_layers_t *layers) {
	bool ok = true;
	for (int e = 0; e < 2; e++) {
		uint64_t t0 = vrr.cfg.settings.timing ? vr_clock_ns() : 0;
		vr_target_t t;
		if (!vrr.cfg.provider.acquire(vrr.cfg.provider.user, (vr_surface_t)(VR_SURFACE_EYE0 + e), &t)) {
			ok = false;
			continue;
		}
		vr_ensure_depth(e, t.width, t.height);
		vr_pass_t pass = {
			.kind = VR_PASS_EYE, .eye = e, .image = t.image, .width = t.width, .height = t.height,
			.depth = vrr.depth_rb[e], .clear = {0, 0, 0, 1}, .segments = 1
		};
		vr_pass(&pass);
		if (vrr.dump_this_frame) {
			vr_dump_queue(e, e == 0 ? "vr-eye0.bmp" : "vr-eye1.bmp", t.image, t.width, t.height, 3);
		}
		vrr.cfg.provider.release(vrr.cfg.provider.user, (vr_surface_t)(VR_SURFACE_EYE0 + e));
		if (t0) {
			vrr.timing.eye_ms[e] = (float)((vr_clock_ns() - t0) * 1e-6);
		}
	}
	layers->projection = ok;
	if (ok) {
		memcpy(layers->views, vrr.views, sizeof(layers->views));
	}
	vrr.timing.replay_ms = vrr.timing.eye_ms[0] + vrr.timing.eye_ms[1];
}

static void vr_replay_hud(vr_layers_t *layers) {
	uint64_t t0 = vrr.cfg.settings.timing ? vr_clock_ns() : 0;
	vr_target_t t;
	if (!vrr.cfg.provider.acquire(vrr.cfg.provider.user, VR_SURFACE_HUD, &t)) {
		return;
	}
	vr_ensure_hud_textures(t.width, t.height);

	// The HUD over black, then over white
	for (int i = 0; i < 2; i++) {
		float v = i == 0 ? 0.0f : 1.0f;
		vr_pass_t pass = {
			.kind = VR_PASS_HUD, .image = vrr.hud_tex[i], .width = t.width, .height = t.height,
			.depth = 0, .clear = {v, v, v, 1}, .segments = 2
		};
		vr_pass(&pass);
	}

	// Solve for the alpha into the swapchain image
	glBindFramebuffer(GL_FRAMEBUFFER, vrr.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.image, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	vr_check_target(t.image);
	glViewport(0, 0, t.width, t.height);
	glDisable(GL_FRAMEBUFFER_SRGB);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glUseProgram(vrr.resolve_program);
	glBindVertexArray(vrr.resolve_vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, vrr.hud_tex[0]);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, vrr.hud_tex[1]);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glActiveTexture(GL_TEXTURE0);
	vrr.draw_calls++;

	if (vrr.dump_this_frame) {
		vr_dump_queue(2, "vr-hud.bmp", t.image, t.width, t.height, 4);
	}
	vrr.cfg.provider.release(vrr.cfg.provider.user, VR_SURFACE_HUD);

	layers->hud = true;
	layers->hud_pose = vrr.hud_pose;
	layers->hud_width = vrr.hud_width;
	layers->hud_height = vrr.hud_height;
	if (t0) {
		vrr.timing.hud_ms = (float)((vr_clock_ns() - t0) * 1e-6);
	}
}

static void vr_replay_mono(vr_layers_t *layers) {
	uint64_t t0 = vrr.cfg.settings.timing ? vr_clock_ns() : 0;
	vr_target_t t;
	if (!vrr.cfg.provider.acquire(vrr.cfg.provider.user, VR_SURFACE_PANEL, &t)) {
		return;
	}
	vr_ensure_depth(2, t.width, t.height);
	vr_pass_t pass = {
		.kind = VR_PASS_MONO, .image = t.image, .width = t.width, .height = t.height,
		.depth = vrr.depth_rb[2], .clear = {0, 0, 0, 1}, .segments = 3
	};
	vr_pass(&pass);
	if (vrr.dump_this_frame) {
		vr_dump_queue(3, "vr-panel.bmp", t.image, t.width, t.height, 3);
	}
	vrr.cfg.provider.release(vrr.cfg.provider.user, VR_SURFACE_PANEL);

	const vr_render_settings_t *st = &vrr.cfg.settings;
	layers->panel = true;
	layers->panel_width = st->panel_width;
	vr_stage_panel_pose(&vrr.stage, vrr.views, vrr.views_ok, st->panel_distance, &layers->panel_pose);
	if (t0) {
		vrr.timing.replay_ms = (float)((vr_clock_ns() - t0) * 1e-6);
	}
}

static void vr_end_frame(void) {
	vrr.recording = false;
	vrr.timing.record_ms = (float)(vrr.rec_ns * 1e-6);

	vr_layers_t layers;
	memset(&layers, 0, sizeof(layers));
	vrr.info = (vr_render_frame_info_t){0};

	if (!vrr.renders) {
		// Standby: the game ran frozen, nothing is drawn
		vrr.cfg.provider.submit(vrr.cfg.provider.user, &layers);
		return;
	}

	if (!vrr.stereo) {
		vr_stage_set_mode(&vrr.stage, VR_MODE_MONO, vrr.now_ns);
	}

	if (!vrr.gl_ready) {
		vr_gl_make();
	}
	vr_gl_upload();
	vrr.frames_replayed++;
	vrr.dump_this_frame = vrr.dump_frame > 0 && vrr.frames_replayed == (uint32_t)vrr.dump_frame && vrr.dump_dir[0];
	if (vrr.dump_this_frame) {
		vr_mkdirs(vrr.dump_dir);
	}

	if (vrr.stereo) {
		vr_replay_eyes(&layers);
		vr_replay_hud(&layers);
	}
	else {
		vr_replay_mono(&layers);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindVertexArray(0);

	layers.fade_alpha = vr_stage_fade_alpha(&vrr.stage, vrr.now_ns);

	vrr.info.mode = vrr.stage.mode;
	vrr.info.frames_replayed = vrr.frames_replayed;
	vrr.info.commands = vrr.list.cmd_len;
	vrr.info.triangles = vrr.list.vert_len / 3;
	vrr.info.sprites = vrr.list.sprite_count;
	vrr.info.draw_calls = vrr.draw_calls;
	vrr.info.fade_alpha = layers.fade_alpha;
	running_stats.num_tris = vrr.info.triangles;
	running_stats.num_draw_calls = vrr.draw_calls;
	memcpy(&end_stats, &running_stats, sizeof(render_stats_t));

	if (vrr.dump_this_frame) {
		vr_dump_state_log();
	}
	vrr.cfg.provider.submit(vrr.cfg.provider.user, &layers);
}
