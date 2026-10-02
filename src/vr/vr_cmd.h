#ifndef VR_CMD_H
#define VR_CMD_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_math.h"

// The frame's renderer command list (docs/VR_ARCHITECTURE.md section 4).
//
// During game_update() every render_* call that would have drawn or changed
// draw state appends a command here instead of touching GL. After the game
// has run, the renderer replays the list: once for a mono frame, once per eye
// and once for the HUD for a stereo frame. Replay never calls game code, so
// per-frame game state advances exactly once.
//
// Texture creation and upload are not commands: they are resources, and stay
// immediate.
//
// Pure data, no GL: the replay lives in render_gl.c (vr_render_gl.inc).

// Same layout as the game's vertex_t (vec3 pos, vec2 uv, rgba color), so the
// arena can go to the GPU as it is. vr_render_gl.inc asserts the sizes match.
typedef struct {
	float pos[3];
	float uv[2];
	uint8_t color[4];
} vr_vertex_t;

typedef enum {
	VR_CMD_VIEW, // render_set_view: depth test and write on, model matrix identity, camera, projection, fade
	VR_CMD_VIEW_2D, // render_set_view_2d: depth test and write off, model identity, ortho projection
	VR_CMD_MODEL_MAT,
	VR_CMD_DEPTH_WRITE,
	VR_CMD_DEPTH_TEST,
	VR_CMD_DEPTH_OFFSET,
	VR_CMD_BLEND,
	VR_CMD_CULL,
	VR_CMD_SCREEN_POS,
	VR_CMD_TRIS, // an index range into the triangle arena, atlas UVs already applied
	VR_CMD_SPRITE // a billboard, expanded at replay facing the replay's camera
} vr_cmd_type_t;

enum {
	VR_CMD_FLAG_2D = 1, // the command was recorded after render_set_view_2d (the HUD segment)
	VR_CMD_FLAG_SKY = 2 // MODEL_MAT of the sky: re-translated to each eye
};

typedef struct {
	uint8_t type;
	uint8_t flags;
	union {
		int32_t cam; // VIEW: index into the list's cameras
		float model[16]; // MODEL_MAT, column-major
		bool on; // DEPTH_WRITE, DEPTH_TEST, CULL
		float offset; // DEPTH_OFFSET
		int32_t blend; // BLEND: render_blend_mode_t
		float screen[2]; // SCREEN_POS, as given to render_set_screen_position
		struct {
			uint32_t first; // triangle index into the arena
			uint32_t count; // triangles
		} tris;
		struct {
			float pos[3];
			int32_t size[2];
			uint8_t color[4];
			uint16_t texture;
		} sprite;
	} u;
} vr_cmd_t;

// What a VIEW command knows about the camera: the game's own (mono) and, in a
// stereo frame, the two eyes and the head-centre camera used to project the
// HUD's target reticle.
typedef struct {
	float game_position[3]; // the position render_set_view got
	vr_cam_t mono; // the game's view_mat and sprite_mat
	bool stereo; // eye[] and hud are valid
	vr_cam_t eye[2];
	vr_cam_t hud; // head centre, roll removed
} vr_camset_t;

typedef struct {
	vr_cmd_t *cmds;
	uint32_t cmd_len, cmd_cap;
	vr_vertex_t *verts; // 3 per triangle
	uint32_t vert_len, vert_cap;
	vr_camset_t *cams;
	uint32_t cam_len, cam_cap;

	// The blend mode and cull state the frame started in (they persist across
	// frames in the flat renderer, so the replay starts from the same state)
	int32_t initial_blend;
	bool initial_cull;

	// Counts for the frame, for stats and logs
	uint32_t sprite_count;
	uint32_t tris_2d, tris_3d; // triangles pushed, not counting sprites
} vr_cmdlist_t;

void vr_cmdlist_init(vr_cmdlist_t *l);
void vr_cmdlist_free(vr_cmdlist_t *l);

// Empties the list, keeping its storage, and records the state the frame starts in.
void vr_cmdlist_reset(vr_cmdlist_t *l, int32_t blend, bool cull);

// A new command of this type; the caller fills in `u`.
vr_cmd_t *vr_cmdlist_push(vr_cmdlist_t *l, vr_cmd_type_t type, uint8_t flags);

// A triangle. Extends the previous TRIS command when that was the last command
// and had the same flags, so a run of pushes is one range.
void vr_cmdlist_push_tris(vr_cmdlist_t *l, const vr_vertex_t v[3], uint8_t flags);

// Stores a camera, returns its index for a VIEW command.
int32_t vr_cmdlist_push_cam(vr_cmdlist_t *l, const vr_camset_t *cam);

#endif
