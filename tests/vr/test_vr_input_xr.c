// The OpenXR side of the VR input against a fake runtime: the action set,
// bindings and attach happen in a valid order, the Frame profile is suggested
// only when its extension is enabled, never the System button, and per-frame
// sync, merge, release on focus loss and haptics reach the right calls.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_xr.h"
#include "wipeout/game.h"

#define HANDLE(n) ((void *)(uintptr_t)(n))
#define INSTANCE HANDLE(0x10)
#define SESSION HANDLE(0x20)
#define ACTION_SET HANDLE(0x30)

#define MAX_ACTIONS 32
#define MAX_PROFILES 4
#define MAX_BINDINGS 32
#define MAX_PATHS 256

// -----------------------------------------------------------------------------
// The fake runtime

typedef struct {
	char profile[96];
	int count;
	char action[MAX_BINDINGS][32];
	char path[MAX_BINDINGS][96];
} profile_t;

static struct {
	// Behaviour
	XrResult create_action_set_result;
	XrResult attach_result;
	XrResult sync_result;
	bool reject_profiles;

	// What the host did
	char calls[128][32];
	int call_count;
	int order_violations;
	bool set_created;
	bool attached;
	int destroyed_sets;

	int action_count;
	char action_name[MAX_ACTIONS][32];
	XrActionType action_type[MAX_ACTIONS];

	char paths[MAX_PATHS][96];
	int path_count;

	profile_t profiles[MAX_PROFILES];
	int profile_count;

	int sync_calls;

	// Input script, by action name index
	bool b_state[MAX_ACTIONS];
	float f_state[MAX_ACTIONS];
	float v_state[MAX_ACTIONS][2];

	// Haptics
	struct {
		int action; // index into the action list
		float amplitude;
		int64_t duration;
		float frequency;
	} haptic[16];
	int haptic_count;
	int stop_count;
} fake;

static void call(const char *name) {
	if (fake.call_count < 128) {
		snprintf(fake.calls[fake.call_count++], 32, "%s", name);
	}
}

static void reset(void) {
	memset(&fake, 0, sizeof(fake));
	fake.create_action_set_result = XR_SUCCESS;
	fake.attach_result = XR_SUCCESS;
	fake.sync_result = XR_SUCCESS;
}

static int action_index(XrAction action) {
	return (int)(uintptr_t)action - 1;
}

static int find_action(const char *name) {
	for (int i = 0; i < fake.action_count; i++) {
		if (strcmp(fake.action_name[i], name) == 0) {
			return i;
		}
	}
	return -1;
}

static XrResult XRAPI_CALL fake_string_to_path(XrInstance instance, const char *name, XrPath *path) {
	(void)instance;
	for (int i = 0; i < fake.path_count; i++) {
		if (strcmp(fake.paths[i], name) == 0) {
			*path = (XrPath)(i + 1);
			return XR_SUCCESS;
		}
	}
	if (fake.path_count >= MAX_PATHS) {
		return XR_ERROR_PATH_COUNT_EXCEEDED;
	}
	snprintf(fake.paths[fake.path_count++], 96, "%s", name);
	*path = (XrPath)fake.path_count;
	return XR_SUCCESS;
}

static const char *path_name(XrPath path) {
	return path >= 1 && path <= (XrPath)fake.path_count ? fake.paths[path - 1] : "";
}

static XrResult XRAPI_CALL fake_create_action_set(XrInstance instance, const XrActionSetCreateInfo *info, XrActionSet *set) {
	(void)instance;
	(void)info;
	call("create_action_set");
	if (fake.create_action_set_result != XR_SUCCESS) {
		return fake.create_action_set_result;
	}
	fake.set_created = true;
	*set = ACTION_SET;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_destroy_action_set(XrActionSet set) {
	(void)set;
	call("destroy_action_set");
	fake.destroyed_sets++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_create_action(XrActionSet set, const XrActionCreateInfo *info, XrAction *action) {
	call("create_action");
	if (set != ACTION_SET || !fake.set_created || fake.attached || fake.action_count >= MAX_ACTIONS) {
		fake.order_violations++; // actions can only be made in a live set, before attach
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	}
	snprintf(fake.action_name[fake.action_count], 32, "%.31s", info->actionName);
	fake.action_type[fake.action_count] = info->actionType;
	fake.action_count++;
	*action = (XrAction)(uintptr_t)fake.action_count;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_suggest(XrInstance instance, const XrInteractionProfileSuggestedBinding *sb) {
	(void)instance;
	call("suggest");
	if (fake.attached || fake.action_count == 0 || fake.profile_count >= MAX_PROFILES) {
		fake.order_violations++; // after attach is an error, and before the actions exist is pointless
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	}
	if (fake.reject_profiles) {
		fake.profile_count++; // still counts as a call
		memset(&fake.profiles[fake.profile_count - 1], 0, sizeof(profile_t));
		snprintf(fake.profiles[fake.profile_count - 1].profile, 96, "%s", path_name(sb->interactionProfile));
		return XR_ERROR_PATH_UNSUPPORTED;
	}
	profile_t *p = &fake.profiles[fake.profile_count++];
	memset(p, 0, sizeof(*p));
	snprintf(p->profile, 96, "%s", path_name(sb->interactionProfile));
	for (uint32_t i = 0; i < sb->countSuggestedBindings && p->count < MAX_BINDINGS; i++) {
		int a = action_index(sb->suggestedBindings[i].action);
		snprintf(p->action[p->count], 32, "%s", a >= 0 && a < fake.action_count ? fake.action_name[a] : "?");
		snprintf(p->path[p->count], 96, "%s", path_name(sb->suggestedBindings[i].binding));
		p->count++;
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_attach(XrSession session, const XrSessionActionSetsAttachInfo *info) {
	call("attach");
	if (session != SESSION || !fake.set_created || info->countActionSets != 1 || info->actionSets[0] != ACTION_SET || fake.attached) {
		fake.order_violations++;
		return XR_ERROR_VALIDATION_FAILURE;
	}
	if (fake.attach_result != XR_SUCCESS) {
		return fake.attach_result;
	}
	fake.attached = true;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_sync(XrSession session, const XrActionsSyncInfo *info) {
	(void)session;
	call("sync");
	fake.sync_calls++;
	if (!fake.attached || info->countActiveActionSets != 1 || info->activeActionSets[0].actionSet != ACTION_SET) {
		fake.order_violations++;
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	}
	return fake.sync_result;
}

static XrResult XRAPI_CALL fake_get_boolean(XrSession session, const XrActionStateGetInfo *info, XrActionStateBoolean *state) {
	(void)session;
	int a = action_index(info->action);
	if (!fake.attached || a < 0 || a >= fake.action_count || fake.action_type[a] != XR_ACTION_TYPE_BOOLEAN_INPUT) {
		fake.order_violations++;
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	}
	state->isActive = XR_TRUE;
	state->currentState = fake.b_state[a] ? XR_TRUE : XR_FALSE;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_get_float(XrSession session, const XrActionStateGetInfo *info, XrActionStateFloat *state) {
	(void)session;
	int a = action_index(info->action);
	if (!fake.attached || a < 0 || a >= fake.action_count || fake.action_type[a] != XR_ACTION_TYPE_FLOAT_INPUT) {
		fake.order_violations++;
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	}
	state->isActive = XR_TRUE;
	state->currentState = fake.f_state[a];
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_get_vector2f(XrSession session, const XrActionStateGetInfo *info, XrActionStateVector2f *state) {
	(void)session;
	int a = action_index(info->action);
	if (!fake.attached || a < 0 || a >= fake.action_count || fake.action_type[a] != XR_ACTION_TYPE_VECTOR2F_INPUT) {
		fake.order_violations++;
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	}
	state->isActive = XR_TRUE;
	state->currentState.x = fake.v_state[a][0];
	state->currentState.y = fake.v_state[a][1];
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_apply_haptic(XrSession session, const XrHapticActionInfo *info, const XrHapticBaseHeader *haptic) {
	(void)session;
	call("haptic");
	const XrHapticVibration *vib = (const XrHapticVibration *)haptic;
	if (!fake.attached || haptic->type != XR_TYPE_HAPTIC_VIBRATION || fake.haptic_count >= 16) {
		fake.order_violations++;
		return XR_ERROR_VALIDATION_FAILURE;
	}
	fake.haptic[fake.haptic_count].action = action_index(info->action);
	fake.haptic[fake.haptic_count].amplitude = vib->amplitude;
	fake.haptic[fake.haptic_count].duration = vib->duration;
	fake.haptic[fake.haptic_count].frequency = vib->frequency;
	fake.haptic_count++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_stop_haptic(XrSession session, const XrHapticActionInfo *info) {
	(void)session;
	(void)info;
	call("stop_haptic");
	fake.stop_count++;
	return XR_SUCCESS;
}

static void make_xr(vr_xr_t *xr, bool frame_extension) {
	memset(xr, 0, sizeof(*xr));
	xr->api.string_to_path = fake_string_to_path;
	xr->api.create_action_set = fake_create_action_set;
	xr->api.destroy_action_set = fake_destroy_action_set;
	xr->api.create_action = fake_create_action;
	xr->api.suggest_interaction_profile_bindings = fake_suggest;
	xr->api.attach_session_action_sets = fake_attach;
	xr->api.sync_actions = fake_sync;
	xr->api.get_action_state_boolean = fake_get_boolean;
	xr->api.get_action_state_float = fake_get_float;
	xr->api.get_action_state_vector2f = fake_get_vector2f;
	xr->api.apply_haptic_feedback = fake_apply_haptic;
	xr->api.stop_haptic_feedback = fake_stop_haptic;
	xr->instance = INSTANCE;
	xr->session = SESSION;
	xr->ext_frame_controller = frame_extension;
}

static void go_focused(vr_xr_t *xr) {
	xr->session_running = true;
	xr->focused = true;
}

static void lose_focus(vr_xr_t *xr) {
	xr->focused = false;
}

static void frame(vr_xr_t *xr, vr_input_t *in, double seconds, float haptics) {
	xr->frame.predicted_display_time = (int64_t)(seconds * 1e9);
	vr_input_update(in, xr, haptics);
}

static void quiet(const char *line) {
	(void)line;
}

// The profile's suggestion by name, NULL when it was not suggested
static const profile_t *profile(const char *name) {
	for (int i = 0; i < fake.profile_count; i++) {
		if (strcmp(fake.profiles[i].profile, name) == 0) {
			return &fake.profiles[i];
		}
	}
	return NULL;
}

// The path an action is bound to in a profile ("" when it is not), and how many it has
static const char *bound_path(const profile_t *p, const char *action, int *count) {
	const char *found = "";
	int n = 0;
	for (int i = 0; i < p->count; i++) {
		if (strcmp(p->action[i], action) == 0) {
			found = p->path[i];
			n++;
		}
	}
	if (count) {
		*count = n;
	}
	return found;
}

#define FRAME "/interaction_profiles/valve/frame_controller_valve"
#define INDEX "/interaction_profiles/valve/index_controller"
#define TOUCH "/interaction_profiles/oculus/touch_controller"


// -----------------------------------------------------------------------------
// Setup order

static void test_setup_order_with_the_frame_extension(void) {
	reset();
	vr_xr_t xr;
	make_xr(&xr, true);
	vr_input_t *in = vr_input_create(&xr);
	CHECK(in != NULL);
	CHECK_INT(fake.order_violations, 0);
	CHECK(fake.attached);

	// create_action_set, every action, the three profiles (Frame first), then attach
	CHECK_STR(fake.calls[0], "create_action_set");
	CHECK(fake.action_count >= 18);
	for (int i = 0; i < fake.action_count; i++) {
		CHECK_STR(fake.calls[1 + i], "create_action");
	}
	int next = 1 + fake.action_count;
	CHECK_STR(fake.calls[next], "suggest");
	CHECK_STR(fake.calls[next + 1], "suggest");
	CHECK_STR(fake.calls[next + 2], "suggest");
	CHECK_STR(fake.calls[next + 3], "attach");
	CHECK_INT(fake.call_count, next + 4); // nothing else

	CHECK_INT(fake.profile_count, 3);
	CHECK_STR(fake.profiles[0].profile, FRAME);
	CHECK_STR(fake.profiles[1].profile, INDEX);
	CHECK_STR(fake.profiles[2].profile, TOUCH);

	vr_input_destroy(in, &xr);
	CHECK_INT(fake.destroyed_sets, 1);
}

static void test_frame_profile_only_with_the_extension(void) {
	reset();
	vr_xr_t xr;
	make_xr(&xr, false);
	vr_input_t *in = vr_input_create(&xr);
	CHECK(in != NULL);
	CHECK_INT(fake.order_violations, 0);
	CHECK(profile(FRAME) == NULL);
	CHECK(profile(INDEX) != NULL);
	CHECK(profile(TOUCH) != NULL);
	CHECK_INT(fake.profile_count, 2);
	CHECK_STR(fake.profiles[0].profile, INDEX);
	CHECK(fake.attached);
	vr_input_destroy(in, &xr);
}

static void test_failures_are_clean(void) {
	vr_xr_t xr;

	// No action set: no input, and nothing attached
	reset();
	fake.create_action_set_result = XR_ERROR_RUNTIME_FAILURE;
	make_xr(&xr, true);
	CHECK(vr_input_create(&xr) == NULL);
	CHECK_INT(fake.action_count, 0);
	CHECK(!fake.attached);

	// Attach fails: the action set is destroyed again
	reset();
	fake.attach_result = XR_ERROR_RUNTIME_FAILURE;
	make_xr(&xr, true);
	CHECK(vr_input_create(&xr) == NULL);
	CHECK_INT(fake.destroyed_sets, 1);

	// A runtime that rejects every profile still attaches, and the game keeps its SDL pad
	reset();
	fake.reject_profiles = true;
	make_xr(&xr, true);
	vr_input_t *in = vr_input_create(&xr);
	CHECK(in != NULL);
	CHECK(fake.attached);
	vr_input_destroy(in, &xr);

	// No session: nothing to attach to
	reset();
	make_xr(&xr, true);
	xr.session = XR_NULL_HANDLE;
	CHECK(vr_input_create(&xr) == NULL);
	CHECK_INT(fake.call_count, 0);

	// NULL input is a no-op everywhere
	vr_input_update(NULL, &xr, 1.0f);
	vr_input_destroy(NULL, &xr);
}


// -----------------------------------------------------------------------------
// Bindings

static void check_every_action_bound_once(const profile_t *p, const char *const *names, int n) {
	for (int i = 0; i < n; i++) {
		int count = 0;
		const char *path = bound_path(p, names[i], &count);
		if (count != 1 || path[0] == '\0') {
			printf("  profile %s, action %s: %d bindings\n", p->profile, names[i], count);
		}
		CHECK_INT(count, 1);
	}
}

static void test_bindings(void) {
	reset();
	vr_xr_t xr;
	make_xr(&xr, true);
	vr_input_t *in = vr_input_create(&xr);
	CHECK(in != NULL);

	const profile_t *frame_p = profile(FRAME);
	const profile_t *index_p = profile(INDEX);
	const profile_t *touch_p = profile(TOUCH);
	CHECK(frame_p && index_p && touch_p);

	// The Frame profile: the full split gamepad, one input per action
	static const char *const frame_actions[] = {
		"a", "b", "x", "y", "l_shoulder", "r_shoulder", "menu", "view", "l_stick_press", "r_stick_press",
		"dpad_up", "dpad_down", "dpad_left", "dpad_right", "l_trigger", "r_trigger", "l_stick", "r_stick",
		"haptic_left", "haptic_right"
	};
	check_every_action_bound_once(frame_p, frame_actions, 20);
	CHECK_STR(bound_path(frame_p, "l_stick", NULL), "/user/hand/left/input/thumbstick");
	CHECK_STR(bound_path(frame_p, "r_stick", NULL), "/user/hand/right/input/thumbstick");
	CHECK_STR(bound_path(frame_p, "a", NULL), "/user/hand/right/input/a/click");
	CHECK_STR(bound_path(frame_p, "b", NULL), "/user/hand/right/input/b/click");
	CHECK_STR(bound_path(frame_p, "x", NULL), "/user/hand/right/input/x/click");
	CHECK_STR(bound_path(frame_p, "y", NULL), "/user/hand/right/input/y/click");
	CHECK_STR(bound_path(frame_p, "l_shoulder", NULL), "/user/hand/left/input/bumper/click");
	CHECK_STR(bound_path(frame_p, "r_shoulder", NULL), "/user/hand/right/input/bumper/click");
	CHECK_STR(bound_path(frame_p, "l_trigger", NULL), "/user/hand/left/input/trigger/value");
	CHECK_STR(bound_path(frame_p, "r_trigger", NULL), "/user/hand/right/input/trigger/value");
	CHECK_STR(bound_path(frame_p, "menu", NULL), "/user/hand/right/input/menu/click");
	CHECK_STR(bound_path(frame_p, "view", NULL), "/user/hand/left/input/view/click");
	CHECK_STR(bound_path(frame_p, "dpad_left", NULL), "/user/hand/left/input/dpad_left/click");
	CHECK_STR(bound_path(frame_p, "haptic_left", NULL), "/user/hand/left/output/haptic");
	CHECK_STR(bound_path(frame_p, "haptic_right", NULL), "/user/hand/right/output/haptic");

	// The fallbacks: every game input and the whole system layer stay reachable
	static const char *const fallback_actions[] = {
		"a", "x", "y", "l_shoulder", "r_shoulder", "menu", "view", "l_trigger", "r_trigger", "l_stick", "r_stick",
		"l_stick_press", "haptic_left", "haptic_right"
	};
	check_every_action_bound_once(index_p, fallback_actions, 14);
	check_every_action_bound_once(touch_p, fallback_actions, 14);
	CHECK_STR(bound_path(index_p, "view", NULL), "/user/hand/left/input/a/click");
	CHECK_STR(bound_path(index_p, "menu", NULL), "/user/hand/left/input/b/click");
	CHECK_STR(bound_path(index_p, "l_shoulder", NULL), "/user/hand/left/input/squeeze/value");
	CHECK_STR(bound_path(touch_p, "view", NULL), "/user/hand/left/input/y/click");
	CHECK_STR(bound_path(touch_p, "menu", NULL), "/user/hand/left/input/menu/click");
	CHECK_STR(bound_path(touch_p, "r_trigger", NULL), "/user/hand/right/input/trigger/value");

	// Rules over every suggestion: user-hand paths only, no System button, no two actions on one input
	for (int p = 0; p < fake.profile_count; p++) {
		const profile_t *prof = &fake.profiles[p];
		for (int i = 0; i < prof->count; i++) {
			CHECK(strncmp(prof->path[i], "/user/hand/", 11) == 0);
			CHECK(strstr(prof->path[i], "system") == NULL);
			for (int j = i + 1; j < prof->count; j++) {
				if (strcmp(prof->path[i], prof->path[j]) == 0) {
					printf("  %s: %s bound twice\n", prof->profile, prof->path[i]);
				}
				CHECK(strcmp(prof->path[i], prof->path[j]) != 0);
			}
		}
	}

	// Action types: the sticks are vec2, the triggers float, the haptics outputs
	CHECK_INT(fake.action_type[find_action("l_stick")], XR_ACTION_TYPE_VECTOR2F_INPUT);
	CHECK_INT(fake.action_type[find_action("r_trigger")], XR_ACTION_TYPE_FLOAT_INPUT);
	CHECK_INT(fake.action_type[find_action("a")], XR_ACTION_TYPE_BOOLEAN_INPUT);
	CHECK_INT(fake.action_type[find_action("haptic_left")], XR_ACTION_TYPE_VIBRATION_OUTPUT);
	vr_input_destroy(in, &xr);
}


// -----------------------------------------------------------------------------
// Per frame

static void bind_game(void) {
	input_init();
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_A, A_THRUST);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_R_TRIGGER, A_FIRE);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_STICK_LEFT, A_LEFT);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_STICK_UP, A_UP);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_SHOULDER, A_BRAKE_LEFT);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_START, A_MENU_START);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT, A_MENU_BACK);
}

static void test_frame_merge_and_focus(void) {
	reset();
	bind_game();
	vr_haptic_clear();
	vr_request_recentre();
	CHECK(vr_take_recentre_request());
	vr_xr_t xr;
	make_xr(&xr, true);
	vr_input_t *in = vr_input_create(&xr);
	CHECK(in != NULL);
	fake.call_count = 0;

	// No focus: no sync at all, nothing written, and nothing to stop
	frame(&xr, in, 10.0, 1.0f);
	CHECK_INT(fake.sync_calls, 0);
	CHECK_INT(fake.stop_count, 0);

	// Focus: the actions are synced and merged into the game's layers
	go_focused(&xr);
	fake.b_state[find_action("a")] = true;
	fake.b_state[find_action("l_shoulder")] = true;
	fake.b_state[find_action("menu")] = true;
	fake.f_state[find_action("r_trigger")] = 0.75f;
	fake.v_state[find_action("l_stick")][0] = -0.5f; // left
	fake.v_state[find_action("l_stick")][1] = 0.9f; // up
	frame(&xr, in, 10.1, 1.0f);
	CHECK_INT(fake.sync_calls, 1);
	CHECK_NEAR(input_state(A_THRUST), 1, 0);
	CHECK_NEAR(input_state(A_BRAKE_LEFT), 1, 0);
	CHECK_NEAR(input_state(A_MENU_START), 1, 0);
	CHECK_NEAR(input_state(A_FIRE), 0.75, 1e-6);
	CHECK_NEAR(input_state(A_LEFT), 0.5, 1e-6);
	CHECK_NEAR(input_state(A_UP), 0.9, 1e-6);
	CHECK(input_pressed(A_THRUST));
	input_clear();

	// Held and unchanged: no new press edge (write on change)
	frame(&xr, in, 10.2, 1.0f);
	CHECK(!input_pressed(A_THRUST));
	CHECK_NEAR(input_state(A_THRUST), 1, 0);

	// An input released
	fake.b_state[find_action("a")] = false;
	frame(&xr, in, 10.3, 1.0f);
	CHECK_NEAR(input_state(A_THRUST), 0, 0);
	CHECK(input_released(A_THRUST));
	input_clear();
	fake.b_state[find_action("a")] = true;
	frame(&xr, in, 10.4, 1.0f);
	CHECK_NEAR(input_state(A_THRUST), 1, 0);

	// Focus lost with everything held and a buzz queued: all released, the buzz dropped and stopped
	vr_haptic_ui_click();
	lose_focus(&xr);
	int syncs = fake.sync_calls;
	fake.haptic_count = 0;
	frame(&xr, in, 10.5, 1.0f);
	CHECK_INT(fake.sync_calls, syncs); // no sync without focus
	CHECK_NEAR(input_state(A_THRUST), 0, 0);
	CHECK_NEAR(input_state(A_BRAKE_LEFT), 0, 0);
	CHECK_NEAR(input_state(A_MENU_START), 0, 0);
	CHECK_NEAR(input_state(A_FIRE), 0, 0);
	CHECK_NEAR(input_state(A_LEFT), 0, 0);
	CHECK_NEAR(input_state(A_UP), 0, 0);
	CHECK_INT(fake.haptic_count, 0);
	CHECK_INT(fake.stop_count, 2); // both hands
	frame(&xr, in, 10.6, 1.0f);
	CHECK_INT(fake.stop_count, 2); // once, on the transition
	input_clear();

	// A haptic queued while unfocused does not play late when focus returns
	vr_haptic_ui_click();
	frame(&xr, in, 10.7, 1.0f);
	go_focused(&xr);
	frame(&xr, in, 10.8, 1.0f);
	CHECK_INT(fake.haptic_count, 0);
	CHECK_NEAR(input_state(A_THRUST), 1, 0); // held through the loss: back

	// The session stopping releases too
	xr.session_running = false;
	frame(&xr, in, 10.9, 1.0f);
	CHECK_NEAR(input_state(A_THRUST), 0, 0);
	xr.session_running = true;

	// NOT_FOCUSED from the runtime itself counts as no focus
	fake.sync_result = XR_SESSION_NOT_FOCUSED;
	frame(&xr, in, 11.0, 1.0f);
	CHECK_NEAR(input_state(A_THRUST), 0, 0);
	fake.sync_result = XR_SUCCESS;

	CHECK_INT(fake.order_violations, 0);
	vr_input_destroy(in, &xr);
	input_clear();
}

static void test_destroy_releases(void) {
	reset();
	bind_game();
	vr_haptic_clear();
	vr_xr_t xr;
	make_xr(&xr, true);
	vr_input_t *in = vr_input_create(&xr);
	go_focused(&xr);
	fake.b_state[find_action("a")] = true;
	frame(&xr, in, 20.0, 1.0f);
	CHECK_NEAR(input_state(A_THRUST), 1, 0);
	vr_input_destroy(in, &xr);
	CHECK_NEAR(input_state(A_THRUST), 0, 0);
	CHECK_INT(fake.destroyed_sets, 1);
	input_clear();
}

static void test_haptics_reach_the_runtime(void) {
	reset();
	bind_game();
	vr_haptic_clear();
	vr_xr_t xr;
	make_xr(&xr, true);
	vr_input_t *in = vr_input_create(&xr);
	go_focused(&xr);
	int left = find_action("haptic_left");
	int right = find_action("haptic_right");

	// A menu click, scaled by vr.haptics, on each hand's own action
	vr_haptic_ui_click();
	frame(&xr, in, 30.0, 0.6f);
	CHECK_INT(fake.haptic_count, 2);
	CHECK_INT(fake.haptic[0].action, left);
	CHECK_INT(fake.haptic[1].action, right);
	CHECK_NEAR(fake.haptic[0].amplitude, 0.3, 1e-6);
	CHECK_NEAR((double)fake.haptic[0].duration, 30000000, 1000); // ns // 30 ms in ns
	CHECK_NEAR(fake.haptic[0].frequency, XR_FREQUENCY_UNSPECIFIED, 0);

	// Once: a frame with nothing queued plays nothing
	frame(&xr, in, 30.1, 0.6f);
	CHECK_INT(fake.haptic_count, 2);

	// A camera shake, both hands
	fake.haptic_count = 0;
	vr_haptic_camera_shake(20.0f / 30.0f);
	frame(&xr, in, 30.2, 1.0f);
	CHECK_INT(fake.haptic_count, 2);
	CHECK_NEAR(fake.haptic[0].amplitude, 1.0, 1e-6);
	CHECK_NEAR((double)fake.haptic[0].duration, 80000000, 1000); // ns

	// vr.haptics = 0 plays nothing
	fake.haptic_count = 0;
	vr_haptic_ui_click();
	frame(&xr, in, 30.3, 0.0f);
	CHECK_INT(fake.haptic_count, 0);

	// Recentre: View held 1 s through the predicted display time, the system buzz on both hands, then the request
	fake.b_state[find_action("view")] = false;
	frame(&xr, in, 40.0, 0.6f);
	fake.b_state[find_action("view")] = true;
	frame(&xr, in, 40.125, 0.6f);
	CHECK(!vr_take_recentre_request());
	fake.haptic_count = 0;
	frame(&xr, in, 41.125, 0.6f);
	CHECK(vr_take_recentre_request());
	CHECK_INT(fake.haptic_count, 2);
	CHECK_NEAR(fake.haptic[0].amplitude, 0.36, 1e-6); // 0.6 x 0.6
	CHECK_NEAR((double)fake.haptic[0].duration, 80000000, 1000); // ns
	fake.b_state[find_action("view")] = false;
	frame(&xr, in, 41.25, 0.6f);
	CHECK(!input_pressed(A_MENU_BACK)); // a hold is not a short press

	CHECK_INT(fake.order_violations, 0);
	vr_input_destroy(in, &xr);
	input_clear();
	vr_haptic_clear();
}

int main(void) {
	vr_log_set_sink(quiet);
	RUN(test_setup_order_with_the_frame_extension);
	RUN(test_frame_profile_only_with_the_extension);
	RUN(test_failures_are_clean);
	RUN(test_bindings);
	RUN(test_frame_merge_and_focus);
	RUN(test_destroy_releases);
	RUN(test_haptics_reach_the_runtime);
	return SFVR_TEST_RESULT("vr_input_xr");
}
