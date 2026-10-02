#ifndef VR_STAGE_H
#define VR_STAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_math.h"
#include "vr_settings.h"
#include "vr_types.h"

// The stereo/mono policy of the renderer, without GL or OpenXR: which mode a
// frame is in, when to recentre, the black fade on a mode switch, where the
// mono panel hangs, and what the frame submits. The inputs are the frame's
// views and the runtime's display time, so tests drive it with numbers.

// The settings the renderer reads, taken once when it is attached.
typedef struct {
	vr_rig_t rig; // world_scale, head_translation, follow_vehicle_rotation, horizon_lock
	float hud_distance; // metres
	float hud_scale; // percent
	int hud_mode; // 0 near, 1 far, 2 leash (only near is built)
	bool force_mono;
	float panel_distance;
	float panel_width;
	float diag_yaw; // radians
	int dump_frame; // 0 = off
	bool timing;
	int ui_width, ui_height; // the 2D layout size: render_size() in VR, and the HUD and panel textures
} vr_render_settings_t;

#define VR_UI_WIDTH 1920
#define VR_UI_HEIGHT 1080

void vr_render_settings_from(vr_render_settings_t *out, const vr_settings_t *s);

typedef enum {
	VR_MODE_NONE, // before the first frame
	VR_MODE_MONO, // the game's own camera and projection, replayed once into the world-locked panel
	VR_MODE_STEREO // a race frame: eyes into the projection layer, 2D into the HUD quad
} vr_mode_t;

// What one frame submits to the compositor. Poses are LOCAL space, metres.
typedef struct {
	bool projection;
	vr_view_t views[2]; // exactly what the eyes were rendered with
	bool hud;
	vr_pose_t hud_pose;
	float hud_width, hud_height;
	bool panel;
	vr_pose_t panel_pose;
	float panel_width;
	float fade_alpha; // 0: no fade quad; else a black quad at this alpha over everything
} vr_layers_t;

#define VR_FADE_HOLD_S 0.25f
#define VR_FADE_OUT_S 0.35f

// Alpha of the transition fade `seconds` after the switch: 1 for the hold,
// then linearly to 0.
float vr_fade_alpha(float seconds);

typedef struct {
	vr_mode_t mode;
	bool recentre_pending; // applied at the next stereo frame
	bool panel_pending; // the mono panel is re-placed at the next mono frame
	bool recentre_valid;
	vr_recentre_t recentre;
	bool panel_valid;
	vr_pose_t panel_pose;
	bool fade_active;
	int64_t fade_start_ns;
	uint32_t mode_switches;
	uint32_t recentres_applied;
} vr_stage_t;

void vr_stage_init(vr_stage_t *s);

// A recentre was asked for (the input layer, focus gain, a reference-space
// change): applies at the next stereo frame, and the panel is re-placed.
void vr_stage_request_recentre(vr_stage_t *s);

// The frame's mode. A change starts the black fade and requests a recentre.
// Returns true on a change.
bool vr_stage_set_mode(vr_stage_t *s, vr_mode_t mode, int64_t now_ns);

// Applies a pending recentre (or the first one) from this frame's head, at the
// start of a stereo frame. Returns true when it did.
bool vr_stage_apply_recentre(vr_stage_t *s, const vr_view_t views[2]);

// Where the mono panel is this frame: placed in front of the head when it is
// pending, then fixed in the world. With no head pose yet it is straight ahead
// of the origin.
void vr_stage_panel_pose(vr_stage_t *s, const vr_view_t views[2], bool views_valid, float distance, vr_pose_t *out);

// The transition fade's alpha for this frame's display time; 0 once it is over.
float vr_stage_fade_alpha(vr_stage_t *s, int64_t now_ns);

#endif
