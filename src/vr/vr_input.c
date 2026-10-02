#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr_input.h"
#include "vr_log.h"
#include "vr_xr.h"

#include "../wipeout/game.h"

// -----------------------------------------------------------------------------
// Requests and haptics: module state, because the game and the renderer call in
// from anywhere without a handle

static bool recentre_requested;
static sfvr_haptic_queue haptic_queue;

void vr_request_recentre(void) {
	recentre_requested = true;
}

bool vr_take_recentre_request(void) {
	bool requested = recentre_requested;
	recentre_requested = false;
	return requested;
}

static float clamp01(float v) {
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

sfvr_haptic_pulse vr_haptic_shake_pulse(float duration_s) {
	float t = clamp01(duration_s / VR_SHAKE_FULL_S);
	sfvr_haptic_pulse pulse = {
		.amplitude = 0.35f + 0.65f * t,
		.seconds = 0.030f + 0.050f * t
	};
	return pulse;
}

void vr_haptic_ui_click(void) {
	sfvr_haptic_event(&haptic_queue, SFVR_HAND_BOTH, SFVR_HAPTIC_UI_CLICK);
}

void vr_haptic_camera_shake(float duration_s) {
	if (duration_s <= 0.0f) {
		return;
	}
	sfvr_haptic_pulse pulse = vr_haptic_shake_pulse(duration_s);
	sfvr_haptic_submit(&haptic_queue, SFVR_HAND_BOTH, pulse.amplitude, pulse.seconds);
}

int vr_haptic_flush(float strength, sfvr_haptic_callback callback, void *user) {
	return sfvr_haptic_flush(&haptic_queue, strength, callback, user);
}

void vr_haptic_clear(void) {
	sfvr_haptic_queue_clear(&haptic_queue);
}


// -----------------------------------------------------------------------------
// Pad mapping

#define PAD_INDEX(code) ((code) - VR_PAD_FIRST)

void vr_pad_init(vr_pad_t *pad) {
	memset(pad, 0, sizeof(*pad));
	sfvr_view_config_default(&pad->view_config);
}

void vr_pad_codes(const vr_pad_sample_t *s, bool select, float out[VR_PAD_COUNT]) {
	memset(out, 0, sizeof(float) * VR_PAD_COUNT);

	out[PAD_INDEX(INPUT_GAMEPAD_A)] = s->a ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_B)] = s->b ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_X)] = s->x ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_Y)] = s->y ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_L_SHOULDER)] = s->l_shoulder ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_R_SHOULDER)] = s->r_shoulder ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_START)] = s->menu ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_SELECT)] = select ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_L_STICK_PRESS)] = s->l_stick_press ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_R_STICK_PRESS)] = s->r_stick_press ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_DPAD_UP)] = s->dpad_up ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_DPAD_DOWN)] = s->dpad_down ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_DPAD_LEFT)] = s->dpad_left ? 1.0f : 0.0f;
	out[PAD_INDEX(INPUT_GAMEPAD_DPAD_RIGHT)] = s->dpad_right ? 1.0f : 0.0f;

	out[PAD_INDEX(INPUT_GAMEPAD_L_TRIGGER)] = clamp01(s->l_trigger);
	out[PAD_INDEX(INPUT_GAMEPAD_R_TRIGGER)] = clamp01(s->r_trigger);

	// Split axes: negative x is LEFT, positive y (stick up) is UP
	out[PAD_INDEX(INPUT_GAMEPAD_L_STICK_LEFT)] = clamp01(-s->l_stick[0]);
	out[PAD_INDEX(INPUT_GAMEPAD_L_STICK_RIGHT)] = clamp01(s->l_stick[0]);
	out[PAD_INDEX(INPUT_GAMEPAD_L_STICK_UP)] = clamp01(s->l_stick[1]);
	out[PAD_INDEX(INPUT_GAMEPAD_L_STICK_DOWN)] = clamp01(-s->l_stick[1]);
	out[PAD_INDEX(INPUT_GAMEPAD_R_STICK_LEFT)] = clamp01(-s->r_stick[0]);
	out[PAD_INDEX(INPUT_GAMEPAD_R_STICK_RIGHT)] = clamp01(s->r_stick[0]);
	out[PAD_INDEX(INPUT_GAMEPAD_R_STICK_UP)] = clamp01(s->r_stick[1]);
	out[PAD_INDEX(INPUT_GAMEPAD_R_STICK_DOWN)] = clamp01(-s->r_stick[1]);
}

unsigned vr_pad_update(vr_pad_t *pad, const vr_pad_sample_t *sample, double now_s, vr_pad_sink_t sink, void *user) {
	// System layer: R Menu is START (above); L View is SELECT on a short press,
	// and a hold recentres, at 1 s and again at 3 s, with the `system` buzz
	unsigned events = sfvr_view_update(&pad->view, &pad->view_config, sample->view ? 1 : 0, now_s);
	if (events & (SFVR_VIEW_RECENTRE | SFVR_VIEW_RECALIBRATE_HEIGHT)) {
		vr_request_recentre();
		sfvr_haptic_event(&haptic_queue, SFVR_HAND_BOTH, SFVR_HAPTIC_SYSTEM);
	}

	float codes[VR_PAD_COUNT];
	vr_pad_codes(sample, (events & SFVR_VIEW_SHORT_PRESS) != 0, codes);
	for (int i = 0; i < VR_PAD_COUNT; i++) {
		if (codes[i] != pad->written[i]) {
			pad->written[i] = codes[i];
			sink((button_t)(VR_PAD_FIRST + i), codes[i], user);
		}
	}
	return events;
}

void vr_pad_release(vr_pad_t *pad, vr_pad_sink_t sink, void *user) {
	for (int i = 0; i < VR_PAD_COUNT; i++) {
		if (pad->written[i] != 0.0f) {
			pad->written[i] = 0.0f;
			sink((button_t)(VR_PAD_FIRST + i), 0.0f, user);
		}
	}
	sfvr_view_reset(&pad->view);
}

void vr_pad_sink_game(button_t button, float value, void *user) {
	(void)user;
	input_set_button_state(button, value);
}


// -----------------------------------------------------------------------------
// Default bindings

const vr_alias_t vr_input_aliases[VR_ALIAS_COUNT] = {
	{ A_LEFT, INPUT_GAMEPAD_L_STICK_LEFT, INPUT_GAMEPAD_DPAD_LEFT },
	{ A_RIGHT, INPUT_GAMEPAD_L_STICK_RIGHT, INPUT_GAMEPAD_DPAD_RIGHT },
	{ A_UP, INPUT_GAMEPAD_L_STICK_UP, INPUT_GAMEPAD_DPAD_UP },
	{ A_DOWN, INPUT_GAMEPAD_L_STICK_DOWN, INPUT_GAMEPAD_DPAD_DOWN },
	{ A_THRUST, INPUT_GAMEPAD_R_TRIGGER, INPUT_GAMEPAD_A }
};

bool vr_alias_applies(const vr_alias_t *alias, uint8_t bound_action, uint8_t pad_slot) {
	return bound_action == INPUT_ACTION_NONE && pad_slot == alias->game_default;
}

int vr_input_apply_default_bindings(uint8_t (*buttons)[2]) {
	int bound = 0;
	for (int i = 0; i < VR_ALIAS_COUNT; i++) {
		const vr_alias_t *alias = &vr_input_aliases[i];
		if (vr_alias_applies(alias, input_bound_to_action(alias->button), buttons[alias->action][1])) {
			input_bind(INPUT_LAYER_USER, alias->button, alias->action);
			bound++;
		}
		else {
			vr_logf("input: alias %s -> action %d not applied (bound by the user or customised)",
				input_button_to_name(alias->button), (int)alias->action);
		}
	}

	// L View, short press: the game's Select, which the game itself never binds. It means menu back.
	if (input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT) == INPUT_ACTION_NONE) {
		input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT, A_MENU_BACK);
	}
	vr_logf("input: %d of %d default aliases bound", bound, VR_ALIAS_COUNT);
	return bound;
}


// -----------------------------------------------------------------------------
// The action set

enum {
	ACT_A,
	ACT_B,
	ACT_X,
	ACT_Y,
	ACT_L_SHOULDER,
	ACT_R_SHOULDER,
	ACT_MENU,
	ACT_VIEW,
	ACT_L_STICK_PRESS,
	ACT_R_STICK_PRESS,
	ACT_DPAD_UP,
	ACT_DPAD_DOWN,
	ACT_DPAD_LEFT,
	ACT_DPAD_RIGHT,
	ACT_L_TRIGGER,
	ACT_R_TRIGGER,
	ACT_L_STICK,
	ACT_R_STICK,
	ACT_HAPTIC_LEFT,
	ACT_HAPTIC_RIGHT,
	ACT_COUNT
};

static const struct {
	const char *name;
	const char *label;
	XrActionType type;
} action_defs[ACT_COUNT] = {
	[ACT_A] = { "a", "A (thrust, select)", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_B] = { "b", "B (back)", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_X] = { "x", "X (fire, back)", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_Y] = { "y", "Y (change view)", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_L_SHOULDER] = { "l_shoulder", "Left air brake", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_R_SHOULDER] = { "r_shoulder", "Right air brake", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_MENU] = { "menu", "Menu (pause)", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_VIEW] = { "view", "View (back, hold to recentre)", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_L_STICK_PRESS] = { "l_stick_press", "Left stick click", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_R_STICK_PRESS] = { "r_stick_press", "Right stick click", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_DPAD_UP] = { "dpad_up", "D-pad up", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_DPAD_DOWN] = { "dpad_down", "D-pad down", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_DPAD_LEFT] = { "dpad_left", "D-pad left", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_DPAD_RIGHT] = { "dpad_right", "D-pad right", XR_ACTION_TYPE_BOOLEAN_INPUT },
	[ACT_L_TRIGGER] = { "l_trigger", "Left trigger", XR_ACTION_TYPE_FLOAT_INPUT },
	[ACT_R_TRIGGER] = { "r_trigger", "Right trigger (thrust)", XR_ACTION_TYPE_FLOAT_INPUT },
	[ACT_L_STICK] = { "l_stick", "Left stick (steer, pitch)", XR_ACTION_TYPE_VECTOR2F_INPUT },
	[ACT_R_STICK] = { "r_stick", "Right stick", XR_ACTION_TYPE_VECTOR2F_INPUT },
	[ACT_HAPTIC_LEFT] = { "haptic_left", "Haptics (left)", XR_ACTION_TYPE_VIBRATION_OUTPUT },
	[ACT_HAPTIC_RIGHT] = { "haptic_right", "Haptics (right)", XR_ACTION_TYPE_VIBRATION_OUTPUT }
};

typedef struct {
	int action;
	const char *path;
} binding_t;

#define PROFILE_FRAME "/interaction_profiles/valve/frame_controller_valve"
#define PROFILE_INDEX "/interaction_profiles/valve/index_controller"
#define PROFILE_TOUCH "/interaction_profiles/oculus/touch_controller"

#define L_ "/user/hand/left/"
#define R_ "/user/hand/right/"

// Steam Frame: a split gamepad. Right hand: A B X Y, Menu, bumper, trigger,
// stick. Left hand: D-pad, View, bumper, trigger, stick. The paths are the ones
// F-Zero X VR and Halo CE VR use for this profile; neither is verified on the device.
static const binding_t frame_bindings[] = {
	{ ACT_L_STICK, L_ "input/thumbstick" },
	{ ACT_R_STICK, R_ "input/thumbstick" },
	{ ACT_A, R_ "input/a/click" },
	{ ACT_B, R_ "input/b/click" },
	{ ACT_X, R_ "input/x/click" },
	{ ACT_Y, R_ "input/y/click" },
	{ ACT_L_SHOULDER, L_ "input/bumper/click" },
	{ ACT_R_SHOULDER, R_ "input/bumper/click" },
	{ ACT_L_TRIGGER, L_ "input/trigger/value" },
	{ ACT_R_TRIGGER, R_ "input/trigger/value" },
	{ ACT_MENU, R_ "input/menu/click" },
	{ ACT_VIEW, L_ "input/view/click" },
	{ ACT_L_STICK_PRESS, L_ "input/thumbstick/click" },
	{ ACT_R_STICK_PRESS, R_ "input/thumbstick/click" },
	{ ACT_DPAD_UP, L_ "input/dpad_up/click" },
	{ ACT_DPAD_DOWN, L_ "input/dpad_down/click" },
	{ ACT_DPAD_LEFT, L_ "input/dpad_left/click" },
	{ ACT_DPAD_RIGHT, L_ "input/dpad_right/click" },
	{ ACT_HAPTIC_LEFT, L_ "output/haptic" },
	{ ACT_HAPTIC_RIGHT, R_ "output/haptic" }
};

// Index (F-Zero X VR's paths, with the rest of the layout reassigned for this
// game): no View, no Menu, no bumpers, no D-pad. The grips are the air brakes,
// the left A button stands in for View, left B for Menu, and the right stick
// click is Y (change view).
static const binding_t index_bindings[] = {
	{ ACT_L_STICK, L_ "input/thumbstick" },
	{ ACT_R_STICK, R_ "input/thumbstick" },
	{ ACT_A, R_ "input/a/click" },
	{ ACT_X, R_ "input/b/click" },
	{ ACT_Y, R_ "input/thumbstick/click" },
	{ ACT_L_SHOULDER, L_ "input/squeeze/value" },
	{ ACT_R_SHOULDER, R_ "input/squeeze/value" },
	{ ACT_L_TRIGGER, L_ "input/trigger/value" },
	{ ACT_R_TRIGGER, R_ "input/trigger/value" },
	{ ACT_MENU, L_ "input/b/click" },
	{ ACT_VIEW, L_ "input/a/click" },
	{ ACT_L_STICK_PRESS, L_ "input/thumbstick/click" },
	{ ACT_HAPTIC_LEFT, L_ "output/haptic" },
	{ ACT_HAPTIC_RIGHT, R_ "output/haptic" }
};

// Touch: the same reassignment. Left Y stands in for View (F-Zero X VR's
// choice), left X is B, and the left Menu button is Menu.
static const binding_t touch_bindings[] = {
	{ ACT_L_STICK, L_ "input/thumbstick" },
	{ ACT_R_STICK, R_ "input/thumbstick" },
	{ ACT_A, R_ "input/a/click" },
	{ ACT_X, R_ "input/b/click" },
	{ ACT_B, L_ "input/x/click" },
	{ ACT_Y, R_ "input/thumbstick/click" },
	{ ACT_L_SHOULDER, L_ "input/squeeze/value" },
	{ ACT_R_SHOULDER, R_ "input/squeeze/value" },
	{ ACT_L_TRIGGER, L_ "input/trigger/value" },
	{ ACT_R_TRIGGER, R_ "input/trigger/value" },
	{ ACT_MENU, L_ "input/menu/click" },
	{ ACT_VIEW, L_ "input/y/click" },
	{ ACT_L_STICK_PRESS, L_ "input/thumbstick/click" },
	{ ACT_HAPTIC_LEFT, L_ "output/haptic" },
	{ ACT_HAPTIC_RIGHT, R_ "output/haptic" }
};

struct vr_input {
	XrActionSet set;
	XrAction action[ACT_COUNT];
	vr_pad_t pad;
	bool active; // the last update had focus
};

static XrPath to_path(struct vr_xr *xr, const char *name) {
	XrPath path = XR_NULL_PATH;
	if (XR_FAILED(xr->api.string_to_path(xr->instance, name, &path))) {
		return XR_NULL_PATH;
	}
	return path;
}

static bool suggest(struct vr_xr *xr, vr_input_t *in, const char *profile, const binding_t *bindings, size_t count) {
	XrActionSuggestedBinding list[ACT_COUNT];
	uint32_t n = 0;
	for (size_t i = 0; i < count && n < ACT_COUNT; i++) {
		XrPath path = to_path(xr, bindings[i].path);
		if (path != XR_NULL_PATH) {
			list[n].action = in->action[bindings[i].action];
			list[n].binding = path;
			n++;
		}
	}
	XrPath profile_path = to_path(xr, profile);
	XrInteractionProfileSuggestedBinding sb = {
		.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
		.interactionProfile = profile_path,
		.countSuggestedBindings = n,
		.suggestedBindings = list
	};
	XrResult r = xr->api.suggest_interaction_profile_bindings(xr->instance, &sb);
	// A profile the runtime does not know is not an error: the others still apply
	vr_logf("input: bindings %s: %s", profile, XR_SUCCEEDED(r) ? "suggested" : "rejected");
	return XR_SUCCEEDED(r);
}

vr_input_t *vr_input_create(struct vr_xr *xr) {
	if (!xr->api.create_action_set || xr->session == XR_NULL_HANDLE) {
		return NULL;
	}
	vr_input_t *in = calloc(1, sizeof(*in));
	if (!in) {
		return NULL;
	}
	vr_pad_init(&in->pad);

	XrActionSetCreateInfo asci = { .type = XR_TYPE_ACTION_SET_CREATE_INFO };
	snprintf(asci.actionSetName, sizeof(asci.actionSetName), "wipeout");
	snprintf(asci.localizedActionSetName, sizeof(asci.localizedActionSetName), "WipEout");
	if (XR_FAILED(xr->api.create_action_set(xr->instance, &asci, &in->set))) {
		vr_logf("input: xrCreateActionSet failed; no VR input");
		free(in);
		return NULL;
	}

	for (int i = 0; i < ACT_COUNT; i++) {
		XrActionCreateInfo aci = { .type = XR_TYPE_ACTION_CREATE_INFO, .actionType = action_defs[i].type };
		snprintf(aci.actionName, sizeof(aci.actionName), "%s", action_defs[i].name);
		snprintf(aci.localizedActionName, sizeof(aci.localizedActionName), "%s", action_defs[i].label);
		if (XR_FAILED(xr->api.create_action(in->set, &aci, &in->action[i]))) {
			vr_logf("input: xrCreateAction %s failed; no VR input", action_defs[i].name);
			xr->api.destroy_action_set(in->set);
			free(in);
			return NULL;
		}
	}

	// The Frame profile first, and only when its extension is enabled; then the fallbacks
	int suggested = 0;
	if (xr->ext_frame_controller) {
		suggested += suggest(xr, in, PROFILE_FRAME, frame_bindings, sizeof(frame_bindings) / sizeof(frame_bindings[0]));
	}
	suggested += suggest(xr, in, PROFILE_INDEX, index_bindings, sizeof(index_bindings) / sizeof(index_bindings[0]));
	suggested += suggest(xr, in, PROFILE_TOUCH, touch_bindings, sizeof(touch_bindings) / sizeof(touch_bindings[0]));
	if (suggested == 0) {
		vr_logf("input: the runtime accepted no interaction profile; no VR input");
	}

	XrSessionActionSetsAttachInfo attach = {
		.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		.countActionSets = 1,
		.actionSets = &in->set
	};
	if (XR_FAILED(xr->api.attach_session_action_sets(xr->session, &attach))) {
		vr_logf("input: xrAttachSessionActionSets failed; no VR input");
		xr->api.destroy_action_set(in->set);
		free(in);
		return NULL;
	}

	vr_logf("input: action set attached (%d actions, %d profiles%s)", ACT_COUNT, suggested,
		xr->ext_frame_controller ? ", frame first" : ", no frame profile");
	return in;
}

void vr_input_destroy(vr_input_t *in, struct vr_xr *xr) {
	if (!in) {
		return;
	}
	vr_pad_release(&in->pad, vr_pad_sink_game, NULL);
	if (in->set != XR_NULL_HANDLE) {
		xr->api.destroy_action_set(in->set);
	}
	free(in);
}

static bool get_bool(struct vr_xr *xr, vr_input_t *in, int action) {
	XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO, .action = in->action[action] };
	XrActionStateBoolean st = { .type = XR_TYPE_ACTION_STATE_BOOLEAN };
	return XR_SUCCEEDED(xr->api.get_action_state_boolean(xr->session, &gi, &st)) && st.isActive && st.currentState;
}

static float get_float(struct vr_xr *xr, vr_input_t *in, int action) {
	XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO, .action = in->action[action] };
	XrActionStateFloat st = { .type = XR_TYPE_ACTION_STATE_FLOAT };
	if (XR_SUCCEEDED(xr->api.get_action_state_float(xr->session, &gi, &st)) && st.isActive) {
		return st.currentState;
	}
	return 0.0f;
}

static void get_vec2(struct vr_xr *xr, vr_input_t *in, int action, float out[2]) {
	XrActionStateGetInfo gi = { .type = XR_TYPE_ACTION_STATE_GET_INFO, .action = in->action[action] };
	XrActionStateVector2f st = { .type = XR_TYPE_ACTION_STATE_VECTOR2F };
	out[0] = out[1] = 0.0f;
	if (XR_SUCCEEDED(xr->api.get_action_state_vector2f(xr->session, &gi, &st)) && st.isActive) {
		out[0] = st.currentState.x;
		out[1] = st.currentState.y;
	}
}

static bool sync_and_sample(struct vr_xr *xr, vr_input_t *in, vr_pad_sample_t *s) {
	XrActiveActionSet active = { .actionSet = in->set, .subactionPath = XR_NULL_PATH };
	XrActionsSyncInfo si = { .type = XR_TYPE_ACTIONS_SYNC_INFO, .countActiveActionSets = 1, .activeActionSets = &active };
	// XR_SESSION_NOT_FOCUSED and XR_SESSION_LOSS_PENDING are success codes with every action inactive
	if (xr->api.sync_actions(xr->session, &si) != XR_SUCCESS) {
		return false;
	}

	memset(s, 0, sizeof(*s));
	s->a = get_bool(xr, in, ACT_A);
	s->b = get_bool(xr, in, ACT_B);
	s->x = get_bool(xr, in, ACT_X);
	s->y = get_bool(xr, in, ACT_Y);
	s->l_shoulder = get_bool(xr, in, ACT_L_SHOULDER);
	s->r_shoulder = get_bool(xr, in, ACT_R_SHOULDER);
	s->menu = get_bool(xr, in, ACT_MENU);
	s->view = get_bool(xr, in, ACT_VIEW);
	s->l_stick_press = get_bool(xr, in, ACT_L_STICK_PRESS);
	s->r_stick_press = get_bool(xr, in, ACT_R_STICK_PRESS);
	s->dpad_up = get_bool(xr, in, ACT_DPAD_UP);
	s->dpad_down = get_bool(xr, in, ACT_DPAD_DOWN);
	s->dpad_left = get_bool(xr, in, ACT_DPAD_LEFT);
	s->dpad_right = get_bool(xr, in, ACT_DPAD_RIGHT);
	s->l_trigger = get_float(xr, in, ACT_L_TRIGGER);
	s->r_trigger = get_float(xr, in, ACT_R_TRIGGER);
	get_vec2(xr, in, ACT_L_STICK, s->l_stick);
	get_vec2(xr, in, ACT_R_STICK, s->r_stick);
	return true;
}

typedef struct {
	struct vr_xr *xr;
	vr_input_t *in;
} haptic_context_t;

static void apply_haptic(int hand, float amplitude, float seconds, void *user) {
	haptic_context_t *ctx = user;
	XrHapticActionInfo hai = {
		.type = XR_TYPE_HAPTIC_ACTION_INFO,
		.action = ctx->in->action[hand == SFVR_HAND_LEFT ? ACT_HAPTIC_LEFT : ACT_HAPTIC_RIGHT]
	};
	XrHapticVibration vib = {
		.type = XR_TYPE_HAPTIC_VIBRATION,
		.amplitude = amplitude,
		.duration = (XrDuration)llround((double)seconds * 1e9),
		.frequency = XR_FREQUENCY_UNSPECIFIED
	};
	XrResult r = ctx->xr->api.apply_haptic_feedback(ctx->xr->session, &hai, (const XrHapticBaseHeader *)&vib);
	if (XR_FAILED(r)) {
		vr_logf("input: haptic failed (%d)", (int)r);
	}
}

static void stop_haptics(struct vr_xr *xr, vr_input_t *in) {
	for (int hand = 0; hand < 2; hand++) {
		XrHapticActionInfo hai = {
			.type = XR_TYPE_HAPTIC_ACTION_INFO,
			.action = in->action[hand == SFVR_HAND_LEFT ? ACT_HAPTIC_LEFT : ACT_HAPTIC_RIGHT]
		};
		xr->api.stop_haptic_feedback(xr->session, &hai);
	}
}

void vr_input_update(vr_input_t *in, struct vr_xr *xr, float haptics) {
	if (!in) {
		return;
	}

	vr_pad_sample_t sample;
	bool live = xr->session_running && xr->focused && sync_and_sample(xr, in, &sample);
	if (!live) {
		// Focus loss or session stop: the game sees every input released, nothing
		// buzzes, and a held View press does not count
		vr_pad_release(&in->pad, vr_pad_sink_game, NULL);
		vr_haptic_clear();
		if (in->active && xr->session != XR_NULL_HANDLE) {
			stop_haptics(xr, in);
		}
		in->active = false;
		return;
	}
	in->active = true;

	double now_s = (double)xr->frame.predicted_display_time * 1e-9;
	vr_pad_update(&in->pad, &sample, now_s, vr_pad_sink_game, NULL);

	haptic_context_t ctx = { xr, in };
	vr_haptic_flush(haptics, apply_haptic, &ctx);
}
