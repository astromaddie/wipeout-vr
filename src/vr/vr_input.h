#ifndef VR_INPUT_H
#define VR_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "../input.h"
#include "sfvr/sfvr_haptics.h"
#include "sfvr/sfvr_view.h"

// OpenXR actions, merged into the game's own pad layer, plus the system layer
// and haptics (docs/VR_ARCHITECTURE.md sections 6 and 7).
//
// The OpenXR action state becomes `input_set_button_state(INPUT_GAMEPAD_*, v)`
// calls, so the game's bindings, prompts and menus stay valid. Nothing here
// binds the System button. This header needs no OpenXR header, so game and
// renderer code can include it.

struct vr_xr;

// -----------------------------------------------------------------------------
// For the game and the renderer

// L View held 1 s, and again at 3 s, asks for a recentre. The stereo renderer
// consumes the request at a frame boundary; this module does not recentre.
void vr_request_recentre(void);
// True once per request (any number of requests in between count as one).
bool vr_take_recentre_request(void);

// Haptic hooks, scaled by vr.haptics when the queue is flushed.
void vr_haptic_ui_click(void);
void vr_haptic_camera_shake(float duration_s); // `impact`, scaled by the shake's duration

// -----------------------------------------------------------------------------
// Pad mapping (pure; tested without a runtime)

// The pad codes the VR input writes: INPUT_GAMEPAD_A .. INPUT_GAMEPAD_R_STICK_RIGHT.
#define VR_PAD_FIRST INPUT_GAMEPAD_A
#define VR_PAD_LAST INPUT_GAMEPAD_R_STICK_RIGHT
#define VR_PAD_COUNT (VR_PAD_LAST - VR_PAD_FIRST + 1)

// The action state of one frame, in the controllers' own terms
typedef struct {
	bool a, b, x, y;
	bool l_shoulder, r_shoulder;
	bool menu; // R Menu
	bool view; // L View, the raw level; the system layer makes SELECT from it
	bool l_stick_press, r_stick_press;
	bool dpad_up, dpad_down, dpad_left, dpad_right;
	float l_trigger, r_trigger; // 0..1
	float l_stick[2], r_stick[2]; // x right, y up, -1..1
} vr_pad_sample_t;

typedef void (*vr_pad_sink_t)(button_t button, float value, void *user);

typedef struct {
	float written[VR_PAD_COUNT]; // what the game was last told, by code - VR_PAD_FIRST
	sfvr_view_button view;
	sfvr_view_config view_config;
} vr_pad_t;

void vr_pad_init(vr_pad_t *pad);

// The value of every pad code for a sample, indexed by code - VR_PAD_FIRST.
// `select` is the system layer's short-press pulse. An analog stick or trigger
// becomes the game's split-axis codes the way platform_sdl.c does it: the
// trigger is its own code, and each stick axis is a pair of codes (LEFT/RIGHT,
// UP/DOWN) of which at most one is non-zero. Stick up is positive y in
// OpenXR and UP here.
void vr_pad_codes(const vr_pad_sample_t *sample, bool select, float out[VR_PAD_COUNT]);

// One frame with focus: the system layer (L View through sfvr_view at `now_s`
// seconds), then `sink` for every code whose value changed since the last
// call. Returns the SFVR_VIEW_* events of this frame. A recentre event calls
// vr_request_recentre() and queues the `system` haptic on both hands.
unsigned vr_pad_update(vr_pad_t *pad, const vr_pad_sample_t *sample, double now_s, vr_pad_sink_t sink, void *user);

// Focus lost, session stopped: sink 0 for every code that is not 0 and cancel
// the L View press, without any system event.
void vr_pad_release(vr_pad_t *pad, vr_pad_sink_t sink, void *user);

// The game's own input layer
void vr_pad_sink_game(button_t button, float value, void *user);

// -----------------------------------------------------------------------------
// Default bindings (VR build, once at startup)

// A VR alias: this pad button also drives this game action, as an addition to
// the game's own bindings. It lives in the user input layer, not in
// save.buttons: save.buttons has one joystick slot per action and is written to
// the save file, so the alias must not replace the D-pad or A there, and a flat
// build reading the same save must not see it.
typedef struct {
	uint8_t action; // the game's action (A_*)
	button_t button; // the code that also drives it
	button_t game_default; // the game's default joystick slot for the action
} vr_alias_t;

#define VR_ALIAS_COUNT 5
extern const vr_alias_t vr_input_aliases[VR_ALIAS_COUNT];

// The rule. An alias applies when nothing is bound to its button in the user
// layer (`bound_action` is INPUT_ACTION_NONE) and the action's joystick slot
// still holds the game's default (`pad_slot`): the user's own binding wins, and
// the controls menu, which shows the slot, never contradicts what the alias does.
bool vr_alias_applies(const vr_alias_t *alias, uint8_t bound_action, uint8_t pad_slot);

// Binds the aliases that apply, given save.buttons, and binds INPUT_GAMEPAD_SELECT
// to A_MENU_BACK in the system layer unless something is bound there. Returns the
// number of aliases bound. Call after the game has bound its own layers.
int vr_input_apply_default_bindings(uint8_t (*buttons)[2]);

// -----------------------------------------------------------------------------
// The OpenXR side

typedef struct vr_input vr_input_t;

// Creates the action set and its actions, suggests the bindings (the Frame
// profile only when XR_VALVE_frame_controller_interaction is enabled, then
// Index and Touch) and attaches the set to the session. Needs a session.
// NULL when the action set could not be made; the game then has no VR input.
vr_input_t *vr_input_create(struct vr_xr *xr);
void vr_input_destroy(vr_input_t *in, struct vr_xr *xr);

// Once per frame, after xrBeginFrame. With focus: xrSyncActions, the merge
// into the game's pad layer, the system layer, and the haptics flush (scaled by
// `haptics`, 0..1). Without focus (or no running session): everything is
// released, queued haptics are dropped and playing ones are stopped. Safe to
// call with `in` NULL.
void vr_input_update(vr_input_t *in, struct vr_xr *xr, float haptics);

// -----------------------------------------------------------------------------
// Haptics (the module's queue; flushed by vr_input_update)

// Amplitude and length of the `impact` pulse for a camera shake of this length:
// 0.35 / 30 ms for none, 1.0 / 80 ms from a full shake (CAMERA_SHAKE_LONG) up.
sfvr_haptic_pulse vr_haptic_shake_pulse(float duration_s);

// Plays the queue through `callback` (hand, amplitude, seconds) scaled by
// `strength` and empties it. vr_input_update uses this with xrApplyHapticFeedback.
int vr_haptic_flush(float strength, sfvr_haptic_callback callback, void *user);
void vr_haptic_clear(void);

#define VR_SHAKE_FULL_S (20.0f * (1.0f / 30.0f)) // CAMERA_SHAKE_LONG in src/wipeout/camera.h

#endif
