// The VR input without a runtime: the mapping from action states to the game's
// pad codes (write on change, release on focus loss), the system layer's timing
// through sfvr_view, the default binding aliases against the game's real input
// layer (src/input.c), and the haptic event table.

#include <stdint.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_input.h"
#include "vr_log.h"
#include "wipeout/game.h"

// -----------------------------------------------------------------------------
// A sink that records what the game would be told

typedef struct {
	button_t button;
	float value;
} write_t;

static write_t writes[256];
static int write_count;

static void record(button_t button, float value, void *user) {
	(void)user;
	if (write_count < 256) {
		writes[write_count].button = button;
		writes[write_count].value = value;
		write_count++;
	}
}

static float last_write(button_t button) {
	for (int i = write_count - 1; i >= 0; i--) {
		if (writes[i].button == button) {
			return writes[i].value;
		}
	}
	return -1.0f;
}

static int writes_to(button_t button) {
	int n = 0;
	for (int i = 0; i < write_count; i++) {
		n += writes[i].button == button;
	}
	return n;
}

static void reset_writes(void) {
	write_count = 0;
}

static float code_of(const float codes[VR_PAD_COUNT], button_t button) {
	return codes[button - VR_PAD_FIRST];
}

// Haptic callback recorder
typedef struct {
	int hand;
	float amplitude, seconds;
} pulse_t;

static pulse_t pulses[8];
static int pulse_count;

static void record_pulse(int hand, float amplitude, float seconds, void *user) {
	(void)user;
	if (pulse_count < 8) {
		pulses[pulse_count].hand = hand;
		pulses[pulse_count].amplitude = amplitude;
		pulses[pulse_count].seconds = seconds;
		pulse_count++;
	}
}

static int flush(float strength) {
	pulse_count = 0;
	return vr_haptic_flush(strength, record_pulse, NULL);
}


// -----------------------------------------------------------------------------
// The mapping

static void test_pad_range(void) {
	CHECK_INT(VR_PAD_FIRST, INPUT_GAMEPAD_A);
	CHECK_INT(VR_PAD_LAST, INPUT_GAMEPAD_R_STICK_RIGHT);
	CHECK_INT(VR_PAD_COUNT, 25);
}

static void test_buttons_map_to_game_codes(void) {
	float codes[VR_PAD_COUNT];
	vr_pad_sample_t s;

	memset(&s, 0, sizeof(s));
	vr_pad_codes(&s, false, codes);
	for (int i = 0; i < VR_PAD_COUNT; i++) {
		CHECK_NEAR(codes[i], 0.0, 0);
	}

	s.a = s.b = s.x = s.y = true;
	s.l_shoulder = s.r_shoulder = true;
	s.menu = true;
	s.l_stick_press = s.r_stick_press = true;
	s.dpad_up = s.dpad_down = s.dpad_left = s.dpad_right = true;
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_A), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_B), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_X), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_Y), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_SHOULDER), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_R_SHOULDER), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_START), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_PRESS), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_R_STICK_PRESS), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_DPAD_UP), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_DPAD_DOWN), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_DPAD_LEFT), 1, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_DPAD_RIGHT), 1, 0);
	// The raw View level is not a code: SELECT comes from the system layer only
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_SELECT), 0, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_HOME), 0, 0);

	memset(&s, 0, sizeof(s));
	s.view = true;
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_SELECT), 0, 0);
	vr_pad_codes(&s, true, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_SELECT), 1, 0);
}

static void test_analog_becomes_split_axes(void) {
	float codes[VR_PAD_COUNT];
	vr_pad_sample_t s;

	// Triggers are their own codes, as SDL's trigger axes are
	memset(&s, 0, sizeof(s));
	s.l_trigger = 0.25f;
	s.r_trigger = 1.5f; // clamped
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_TRIGGER), 0.25, 1e-6);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_R_TRIGGER), 1.0, 1e-6);

	// Left stick: x right is RIGHT, x left is LEFT; y up is UP, y down is DOWN. Never both of a pair
	memset(&s, 0, sizeof(s));
	s.l_stick[0] = 0.5f;
	s.l_stick[1] = 0.8f;
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_RIGHT), 0.5, 1e-6);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_LEFT), 0, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_UP), 0.8, 1e-6);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_DOWN), 0, 0);

	s.l_stick[0] = -0.3f;
	s.l_stick[1] = -1.2f; // clamped
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_RIGHT), 0, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_LEFT), 0.3, 1e-6);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_UP), 0, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_DOWN), 1.0, 1e-6);

	// The right stick maps the same way, to its own codes
	memset(&s, 0, sizeof(s));
	s.r_stick[0] = -1.0f;
	s.r_stick[1] = 0.5f;
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_R_STICK_LEFT), 1.0, 1e-6);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_R_STICK_UP), 0.5, 1e-6);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_L_STICK_LEFT), 0, 0);
}

static void test_write_on_change(void) {
	vr_pad_t pad;
	vr_pad_sample_t s;

	vr_pad_init(&pad);
	memset(&s, 0, sizeof(s));
	reset_writes();

	// Nothing held: nothing written, however many frames
	for (int i = 0; i < 5; i++) {
		vr_pad_update(&pad, &s, 10.0 + i * 0.01, record, NULL);
	}
	CHECK_INT(write_count, 0);

	// A press is one write, held frames none, the release one write of 0
	s.a = true;
	vr_pad_update(&pad, &s, 10.1, record, NULL);
	CHECK_INT(write_count, 1);
	CHECK_INT(writes[0].button, INPUT_GAMEPAD_A);
	CHECK_NEAR(writes[0].value, 1, 0);
	for (int i = 0; i < 5; i++) {
		vr_pad_update(&pad, &s, 10.2 + i * 0.01, record, NULL);
	}
	CHECK_INT(write_count, 1);
	s.a = false;
	vr_pad_update(&pad, &s, 10.3, record, NULL);
	CHECK_INT(write_count, 2);
	CHECK_INT(writes[1].button, INPUT_GAMEPAD_A);
	CHECK_NEAR(writes[1].value, 0, 0);

	// An analog value is written when it changes, and only that code
	reset_writes();
	s.r_trigger = 0.4f;
	vr_pad_update(&pad, &s, 10.4, record, NULL);
	vr_pad_update(&pad, &s, 10.5, record, NULL);
	s.r_trigger = 0.6f;
	vr_pad_update(&pad, &s, 10.6, record, NULL);
	CHECK_INT(write_count, 2);
	CHECK_INT(writes[0].button, INPUT_GAMEPAD_R_TRIGGER);
	CHECK_NEAR(writes[0].value, 0.4, 1e-6);
	CHECK_NEAR(writes[1].value, 0.6, 1e-6);

	// A stick moved from right to left: RIGHT goes to 0 and LEFT is set, once each
	reset_writes();
	memset(&s, 0, sizeof(s));
	s.l_stick[0] = 0.7f;
	vr_pad_update(&pad, &s, 10.7, record, NULL);
	CHECK_INT(writes_to(INPUT_GAMEPAD_L_STICK_RIGHT), 1);
	s.l_stick[0] = -0.7f;
	vr_pad_update(&pad, &s, 10.8, record, NULL);
	CHECK_INT(writes_to(INPUT_GAMEPAD_L_STICK_RIGHT), 2);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_L_STICK_RIGHT), 0, 0);
	CHECK_INT(writes_to(INPUT_GAMEPAD_L_STICK_LEFT), 1);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_L_STICK_LEFT), 0.7, 1e-6);
}

static void test_release_on_focus_loss(void) {
	vr_pad_t pad;
	vr_pad_sample_t s;

	vr_pad_init(&pad);
	memset(&s, 0, sizeof(s));
	s.a = true;
	s.r_shoulder = true;
	s.l_stick[0] = -1.0f;
	s.r_trigger = 0.5f;
	s.menu = true;
	vr_pad_update(&pad, &s, 20.0, record, NULL);
	reset_writes();

	// Focus lost: every held code is released, once, and only those
	vr_pad_release(&pad, record, NULL);
	CHECK_INT(write_count, 5);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_A), 0, 0);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_R_SHOULDER), 0, 0);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_L_STICK_LEFT), 0, 0);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_R_TRIGGER), 0, 0);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_START), 0, 0);
	CHECK_INT(writes_to(INPUT_GAMEPAD_B), 0);

	// While focus is away the release runs every frame and writes nothing more
	vr_pad_release(&pad, record, NULL);
	vr_pad_release(&pad, record, NULL);
	CHECK_INT(write_count, 5);

	// Focus back with the same inputs still held: they are written again
	reset_writes();
	vr_pad_update(&pad, &s, 25.0, record, NULL);
	CHECK_INT(write_count, 5);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_A), 1, 0);
}

static void test_game_input_layer(void) {
	// The same, against the game's real input layer
	input_init();
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_A, A_THRUST);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_X, A_FIRE);

	vr_pad_t pad;
	vr_pad_sample_t s;
	vr_pad_init(&pad);
	memset(&s, 0, sizeof(s));
	s.a = true;
	vr_pad_update(&pad, &s, 30.0, vr_pad_sink_game, NULL);
	CHECK_NEAR(input_state(A_THRUST), 1, 0);
	CHECK(input_pressed(A_THRUST));
	CHECK_NEAR(input_state(A_FIRE), 0, 0);

	vr_pad_release(&pad, vr_pad_sink_game, NULL);
	CHECK_NEAR(input_state(A_THRUST), 0, 0);
	CHECK(input_released(A_THRUST));
	input_clear();
}


// -----------------------------------------------------------------------------
// The system layer, through sfvr_view

static void test_system_short_press(void) {
	vr_pad_t pad;
	vr_pad_sample_t s;

	vr_pad_init(&pad);
	vr_request_recentre();
	CHECK(vr_take_recentre_request()); // clear any earlier state
	vr_haptic_clear();
	memset(&s, 0, sizeof(s));
	reset_writes();

	vr_pad_update(&pad, &s, 100.0, record, NULL);
	s.view = true;
	CHECK_INT(vr_pad_update(&pad, &s, 100.1, record, NULL), 0);
	CHECK_INT(write_count, 0); // nothing until the release
	CHECK_INT(vr_pad_update(&pad, &s, 100.5, record, NULL), 0);
	s.view = false;
	CHECK_INT(vr_pad_update(&pad, &s, 100.6, record, NULL), SFVR_VIEW_SHORT_PRESS);
	// SELECT is one frame of 1, then 0
	CHECK_INT(writes_to(INPUT_GAMEPAD_SELECT), 1);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_SELECT), 1, 0);
	CHECK_INT(vr_pad_update(&pad, &s, 100.7, record, NULL), 0);
	CHECK_INT(writes_to(INPUT_GAMEPAD_SELECT), 2);
	CHECK_NEAR(last_write(INPUT_GAMEPAD_SELECT), 0, 0);
	vr_pad_update(&pad, &s, 100.8, record, NULL);
	CHECK_INT(writes_to(INPUT_GAMEPAD_SELECT), 2);

	// A short press does not recentre and does not buzz
	CHECK(!vr_take_recentre_request());
	CHECK_INT(flush(1.0f), 0);
}

static void test_system_hold_recentres_at_1s_and_3s(void) {
	vr_pad_t pad;
	vr_pad_sample_t s;

	vr_pad_init(&pad);
	CHECK(!vr_take_recentre_request());
	vr_haptic_clear();
	memset(&s, 0, sizeof(s));
	reset_writes();

	vr_pad_update(&pad, &s, 200.0, record, NULL);
	s.view = true;
	vr_pad_update(&pad, &s, 200.0, record, NULL); // pressed at t = 200
	CHECK_INT(vr_pad_update(&pad, &s, 200.9, record, NULL), 0);
	CHECK(!vr_take_recentre_request());
	CHECK_INT(flush(1.0f), 0);

	// 1 s: recentre, and the system buzz on both hands
	CHECK_INT(vr_pad_update(&pad, &s, 201.0, record, NULL), SFVR_VIEW_RECENTRE);
	CHECK(vr_take_recentre_request());
	CHECK(!vr_take_recentre_request()); // taken once
	CHECK_INT(flush(1.0f), 2);
	CHECK_INT(pulses[0].hand, SFVR_HAND_LEFT);
	CHECK_INT(pulses[1].hand, SFVR_HAND_RIGHT);
	CHECK_NEAR(pulses[0].amplitude, 0.6, 1e-6);
	CHECK_NEAR(pulses[0].seconds, 0.08, 1e-6);

	// Nothing again until 3 s
	CHECK_INT(vr_pad_update(&pad, &s, 202.5, record, NULL), 0);
	CHECK(!vr_take_recentre_request());
	CHECK_INT(flush(1.0f), 0);
	CHECK_INT(vr_pad_update(&pad, &s, 203.0, record, NULL), SFVR_VIEW_RECALIBRATE_HEIGHT);
	CHECK(vr_take_recentre_request()); // recentre again; this seated racer has no height
	CHECK_INT(flush(1.0f), 2);

	// The release of a long hold is not a short press: SELECT is never written
	s.view = false;
	CHECK_INT(vr_pad_update(&pad, &s, 203.2, record, NULL), 0);
	CHECK_INT(writes_to(INPUT_GAMEPAD_SELECT), 0);
	CHECK(!vr_take_recentre_request());
}

static void test_system_late_update_requests_once(void) {
	vr_pad_t pad;
	vr_pad_sample_t s;

	vr_pad_init(&pad);
	vr_haptic_clear();
	CHECK(!vr_take_recentre_request());
	memset(&s, 0, sizeof(s));
	vr_pad_update(&pad, &s, 300.0, record, NULL);
	s.view = true;
	vr_pad_update(&pad, &s, 300.0, record, NULL);
	// One very late frame sees both thresholds: one request, one coalesced buzz
	CHECK_INT(vr_pad_update(&pad, &s, 304.0, record, NULL), SFVR_VIEW_RECENTRE | SFVR_VIEW_RECALIBRATE_HEIGHT);
	CHECK(vr_take_recentre_request());
	CHECK(!vr_take_recentre_request());
	CHECK_INT(flush(1.0f), 2);
}

static void test_system_focus_loss_cancels_the_press(void) {
	vr_pad_t pad;
	vr_pad_sample_t s;

	vr_pad_init(&pad);
	vr_haptic_clear();
	CHECK(!vr_take_recentre_request());
	memset(&s, 0, sizeof(s));
	vr_pad_update(&pad, &s, 400.0, record, NULL);
	s.view = true;
	vr_pad_update(&pad, &s, 400.1, record, NULL);
	vr_pad_update(&pad, &s, 400.8, record, NULL);

	vr_pad_release(&pad, record, NULL); // focus lost at 0.7 s of the hold
	// Focus back, View still down: the press is ignored until released and pressed again
	reset_writes();
	CHECK_INT(vr_pad_update(&pad, &s, 405.0, record, NULL), 0);
	CHECK_INT(vr_pad_update(&pad, &s, 407.0, record, NULL), 0);
	CHECK(!vr_take_recentre_request());
	s.view = false;
	CHECK_INT(vr_pad_update(&pad, &s, 407.1, record, NULL), 0);
	CHECK_INT(writes_to(INPUT_GAMEPAD_SELECT), 0);
	CHECK_INT(flush(1.0f), 0);

	// And a fresh press works again
	s.view = true;
	vr_pad_update(&pad, &s, 408.0, record, NULL);
	s.view = false;
	CHECK_INT(vr_pad_update(&pad, &s, 408.1, record, NULL), SFVR_VIEW_SHORT_PRESS);
}

static void test_system_never_binds_the_system_button(void) {
	// The module knows no code for the System button, and the sample has no field for it
	float codes[VR_PAD_COUNT];
	vr_pad_sample_t s;
	memset(&s, 0, sizeof(s));
	s.view = s.menu = true;
	vr_pad_codes(&s, false, codes);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_HOME), 0, 0);
	CHECK_NEAR(code_of(codes, INPUT_GAMEPAD_START), 1, 0); // R Menu
}


// -----------------------------------------------------------------------------
// Default aliases, against the game's real input layer

// The game's defaults (game.c) for the joystick slot of each action
static void default_buttons(uint8_t buttons[NUM_GAME_ACTIONS][2]) {
	memset(buttons, 0, sizeof(uint8_t) * NUM_GAME_ACTIONS * 2);
	buttons[A_UP][1] = INPUT_GAMEPAD_DPAD_UP;
	buttons[A_DOWN][1] = INPUT_GAMEPAD_DPAD_DOWN;
	buttons[A_LEFT][1] = INPUT_GAMEPAD_DPAD_LEFT;
	buttons[A_RIGHT][1] = INPUT_GAMEPAD_DPAD_RIGHT;
	buttons[A_BRAKE_LEFT][1] = INPUT_GAMEPAD_L_SHOULDER;
	buttons[A_BRAKE_RIGHT][1] = INPUT_GAMEPAD_R_SHOULDER;
	buttons[A_THRUST][1] = INPUT_GAMEPAD_A;
	buttons[A_FIRE][1] = INPUT_GAMEPAD_X;
	buttons[A_CHANGE_VIEW][1] = INPUT_GAMEPAD_Y;
}

// What game_init does: the fixed system binds that matter here, then the user layer from save.buttons
static void bind_like_the_game(uint8_t buttons[NUM_GAME_ACTIONS][2]) {
	input_init();
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_L_STICK_LEFT, A_MENU_LEFT);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_L_STICK_RIGHT, A_MENU_RIGHT);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_L_STICK_UP, A_MENU_UP);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_L_STICK_DOWN, A_MENU_DOWN);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_X, A_MENU_BACK);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_B, A_MENU_BACK);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_A, A_MENU_SELECT);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_START, A_MENU_START);
	for (int action = 0; action < NUM_GAME_ACTIONS; action++) {
		for (int slot = 0; slot < 2; slot++) {
			if (buttons[action][slot] != INPUT_INVALID) {
				input_bind(INPUT_LAYER_USER, buttons[action][slot], action);
			}
		}
	}
}

static void release_all(void) {
	for (int b = INPUT_GAMEPAD_A; b <= INPUT_GAMEPAD_R_STICK_RIGHT; b++) {
		input_set_button_state(b, 0);
	}
	input_clear();
}

static void test_alias_rule(void) {
	const vr_alias_t thrust = { A_THRUST, INPUT_GAMEPAD_R_TRIGGER, INPUT_GAMEPAD_A };
	CHECK(vr_alias_applies(&thrust, INPUT_ACTION_NONE, INPUT_GAMEPAD_A));
	CHECK(!vr_alias_applies(&thrust, A_FIRE, INPUT_GAMEPAD_A)); // the user bound the trigger
	CHECK(!vr_alias_applies(&thrust, INPUT_ACTION_NONE, INPUT_GAMEPAD_B)); // the user rebound thrust's slot
	CHECK(!vr_alias_applies(&thrust, INPUT_ACTION_NONE, INPUT_INVALID)); // or unbound it
	CHECK(!vr_alias_applies(&thrust, A_THRUST, INPUT_GAMEPAD_A)); // already bound (also: a second call)

	// The table: the L stick steers and pitches, the R trigger thrusts
	CHECK_INT(VR_ALIAS_COUNT, 5);
	int found = 0;
	for (int i = 0; i < VR_ALIAS_COUNT; i++) {
		const vr_alias_t *a = &vr_input_aliases[i];
		if (a->action == A_LEFT) { CHECK_INT(a->button, INPUT_GAMEPAD_L_STICK_LEFT); CHECK_INT(a->game_default, INPUT_GAMEPAD_DPAD_LEFT); found++; }
		if (a->action == A_RIGHT) { CHECK_INT(a->button, INPUT_GAMEPAD_L_STICK_RIGHT); CHECK_INT(a->game_default, INPUT_GAMEPAD_DPAD_RIGHT); found++; }
		if (a->action == A_UP) { CHECK_INT(a->button, INPUT_GAMEPAD_L_STICK_UP); CHECK_INT(a->game_default, INPUT_GAMEPAD_DPAD_UP); found++; }
		if (a->action == A_DOWN) { CHECK_INT(a->button, INPUT_GAMEPAD_L_STICK_DOWN); CHECK_INT(a->game_default, INPUT_GAMEPAD_DPAD_DOWN); found++; }
		if (a->action == A_THRUST) { CHECK_INT(a->button, INPUT_GAMEPAD_R_TRIGGER); CHECK_INT(a->game_default, INPUT_GAMEPAD_A); found++; }
	}
	CHECK_INT(found, 5);
}

static void test_aliases_on_defaults(void) {
	uint8_t buttons[NUM_GAME_ACTIONS][2];
	default_buttons(buttons);
	bind_like_the_game(buttons);
	CHECK_INT(vr_input_apply_default_bindings(buttons), 5);

	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_LEFT), A_LEFT);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_RIGHT), A_RIGHT);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_UP), A_UP);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_DOWN), A_DOWN);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_R_TRIGGER), A_THRUST);

	// Every existing binding is still there
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_DPAD_LEFT), A_LEFT);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_DPAD_RIGHT), A_RIGHT);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_DPAD_UP), A_UP);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_DPAD_DOWN), A_DOWN);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_A), A_THRUST);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_X), A_FIRE);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_Y), A_CHANGE_VIEW);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_SHOULDER), A_BRAKE_LEFT);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_R_SHOULDER), A_BRAKE_RIGHT);
	// The menu's system layer keeps the stick too
	CHECK_INT(input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_L_STICK_LEFT), A_MENU_LEFT);
	CHECK_INT(input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_A), A_MENU_SELECT);
	CHECK_INT(input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_START), A_MENU_START);

	// The save's buttons, which the controls menu shows, are untouched
	uint8_t expected[NUM_GAME_ACTIONS][2];
	default_buttons(expected);
	CHECK(memcmp(buttons, expected, sizeof(expected)) == 0);

	// L View short press is menu back, in the system layer
	CHECK_INT(input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT), A_MENU_BACK);

	// And they work: the stick steers, the trigger thrusts, and the same stick still moves the menu
	input_set_button_state(INPUT_GAMEPAD_L_STICK_LEFT, 0.8f);
	CHECK_NEAR(input_state(A_LEFT), 0.8, 1e-6);
	CHECK(input_pressed(A_MENU_LEFT));
	input_set_button_state(INPUT_GAMEPAD_R_TRIGGER, 0.5f);
	CHECK_NEAR(input_state(A_THRUST), 0.5, 1e-6);
	input_set_button_state(INPUT_GAMEPAD_L_STICK_UP, 1.0f);
	CHECK_NEAR(input_state(A_UP), 1.0, 1e-6);
	release_all();
	CHECK_NEAR(input_state(A_LEFT), 0, 0);
	CHECK_NEAR(input_state(A_THRUST), 0, 0);

	// Applying twice changes nothing (the bound buttons now count as bound)
	CHECK_INT(vr_input_apply_default_bindings(buttons), 0);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_R_TRIGGER), A_THRUST);
	release_all();
}

static void test_aliases_do_not_clobber_the_user(void) {
	uint8_t buttons[NUM_GAME_ACTIONS][2];

	// The user bound the R trigger to fire and the L stick's left to brake
	default_buttons(buttons);
	buttons[A_FIRE][1] = INPUT_GAMEPAD_R_TRIGGER;
	buttons[A_BRAKE_LEFT][1] = INPUT_GAMEPAD_L_STICK_LEFT;
	bind_like_the_game(buttons);
	CHECK_INT(vr_input_apply_default_bindings(buttons), 3);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_R_TRIGGER), A_FIRE); // not thrust
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_LEFT), A_BRAKE_LEFT); // not left
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_RIGHT), A_RIGHT);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_UP), A_UP);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_DOWN), A_DOWN);
	release_all();

	// The user rebound thrust's joystick slot: the trigger alias stays off, and
	// so does the stick alias of an action whose slot is not the D-pad any more
	default_buttons(buttons);
	buttons[A_THRUST][1] = INPUT_GAMEPAD_B;
	buttons[A_UP][1] = INPUT_GAMEPAD_L_SHOULDER;
	bind_like_the_game(buttons);
	CHECK_INT(vr_input_apply_default_bindings(buttons), 3);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_R_TRIGGER), INPUT_ACTION_NONE);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_UP), INPUT_ACTION_NONE);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_B), A_THRUST);
	release_all();

	// A joystick slot the user cleared counts as customised too
	default_buttons(buttons);
	buttons[A_LEFT][1] = INPUT_INVALID;
	bind_like_the_game(buttons);
	CHECK_INT(vr_input_apply_default_bindings(buttons), 4);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_L_STICK_LEFT), INPUT_ACTION_NONE);
	release_all();
}

static void test_select_binding_does_not_clobber(void) {
	uint8_t buttons[NUM_GAME_ACTIONS][2];

	// Something already bound to SELECT in the system layer stays
	default_buttons(buttons);
	bind_like_the_game(buttons);
	input_bind(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT, A_MENU_START);
	vr_input_apply_default_bindings(buttons);
	CHECK_INT(input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT), A_MENU_START);

	// A user binding of SELECT is another layer: it stays too, and the system layer gets back
	default_buttons(buttons);
	buttons[A_CHANGE_VIEW][1] = INPUT_GAMEPAD_SELECT;
	bind_like_the_game(buttons);
	vr_input_apply_default_bindings(buttons);
	CHECK_INT(input_bound_to_action(INPUT_GAMEPAD_SELECT), A_CHANGE_VIEW);
	CHECK_INT(input_layer_bound_to_action(INPUT_LAYER_SYSTEM, INPUT_GAMEPAD_SELECT), A_MENU_BACK);
	release_all();
}

static void test_short_press_goes_back_in_menus(void) {
	// The whole chain: a View short press through the system layer reaches A_MENU_BACK
	uint8_t buttons[NUM_GAME_ACTIONS][2];
	default_buttons(buttons);
	bind_like_the_game(buttons);
	vr_input_apply_default_bindings(buttons);

	vr_pad_t pad;
	vr_pad_sample_t s;
	vr_pad_init(&pad);
	memset(&s, 0, sizeof(s));
	vr_pad_update(&pad, &s, 500.0, vr_pad_sink_game, NULL);
	s.view = true;
	vr_pad_update(&pad, &s, 500.1, vr_pad_sink_game, NULL);
	s.view = false;
	vr_pad_update(&pad, &s, 500.2, vr_pad_sink_game, NULL);
	CHECK(input_pressed(A_MENU_BACK));
	input_clear();
	vr_pad_update(&pad, &s, 500.3, vr_pad_sink_game, NULL);
	CHECK(input_released(A_MENU_BACK));
	release_all();
	vr_haptic_clear();
}


// -----------------------------------------------------------------------------
// Haptics

static void test_haptic_event_table(void) {
	// The standard's table, from sfvr
	sfvr_haptic_pulse click = sfvr_haptic_default(SFVR_HAPTIC_UI_CLICK);
	CHECK_NEAR(click.amplitude, 0.5, 1e-6);
	CHECK_NEAR(click.seconds, 0.030, 1e-6);
	sfvr_haptic_pulse system = sfvr_haptic_default(SFVR_HAPTIC_SYSTEM);
	CHECK_NEAR(system.amplitude, 0.6, 1e-6);
	CHECK_NEAR(system.seconds, 0.080, 1e-6);

	// ui_click: both hands, scaled by vr.haptics
	vr_haptic_clear();
	vr_haptic_ui_click();
	CHECK_INT(flush(0.6f), 2);
	CHECK_INT(pulses[0].hand, SFVR_HAND_LEFT);
	CHECK_INT(pulses[1].hand, SFVR_HAND_RIGHT);
	CHECK_NEAR(pulses[0].amplitude, 0.3, 1e-6); // 0.5 x 0.6
	CHECK_NEAR(pulses[0].seconds, 0.030, 1e-6); // the length is not scaled
	CHECK_NEAR(pulses[1].amplitude, 0.3, 1e-6);
	CHECK_INT(flush(0.6f), 0); // the queue was emptied

	// vr.haptics = 0 plays nothing; 1 plays the table
	vr_haptic_ui_click();
	CHECK_INT(flush(0.0f), 0);
	vr_haptic_ui_click();
	CHECK_INT(flush(1.0f), 2);
	CHECK_NEAR(pulses[0].amplitude, 0.5, 1e-6);
}

static void test_haptic_shake_impact(void) {
	// Amplitude and length grow with the shake's duration, inside the standard's impact range
	sfvr_haptic_pulse none = vr_haptic_shake_pulse(0.0f);
	CHECK_NEAR(none.amplitude, 0.35, 1e-6);
	CHECK_NEAR(none.seconds, 0.030, 1e-6);
	sfvr_haptic_pulse shortp = vr_haptic_shake_pulse(2.0f / 30.0f); // CAMERA_SHAKE_SHORT
	sfvr_haptic_pulse longp = vr_haptic_shake_pulse(20.0f / 30.0f); // CAMERA_SHAKE_LONG
	CHECK_NEAR(shortp.amplitude, 0.35 + 0.65 * 0.1, 1e-5);
	CHECK_NEAR(shortp.seconds, 0.030 + 0.050 * 0.1, 1e-6);
	CHECK_NEAR(longp.amplitude, 1.0, 1e-6);
	CHECK_NEAR(longp.seconds, 0.080, 1e-6);
	CHECK(shortp.amplitude > none.amplitude && longp.amplitude > shortp.amplitude);
	sfvr_haptic_pulse huge = vr_haptic_shake_pulse(100.0f);
	CHECK_NEAR(huge.amplitude, 1.0, 1e-6); // capped
	CHECK_NEAR(huge.seconds, 0.080, 1e-6);
	sfvr_haptic_pulse negative = vr_haptic_shake_pulse(-1.0f);
	CHECK_NEAR(negative.amplitude, 0.35, 1e-6);

	// The hook: both hands, scaled by vr.haptics
	vr_haptic_clear();
	vr_haptic_camera_shake(20.0f / 30.0f);
	CHECK_INT(flush(0.6f), 2);
	CHECK_NEAR(pulses[0].amplitude, 0.6, 1e-6);
	CHECK_NEAR(pulses[0].seconds, 0.080, 1e-6);

	// A shake of 0 s (or less) is no impact
	vr_haptic_camera_shake(0.0f);
	vr_haptic_camera_shake(-1.0f);
	CHECK_INT(flush(1.0f), 0);

	// Overlapping events become one pulse: the strongest amplitude, the longest length
	vr_haptic_camera_shake(2.0f / 30.0f);
	vr_haptic_ui_click();
	vr_haptic_camera_shake(20.0f / 30.0f);
	CHECK_INT(flush(1.0f), 2);
	CHECK_NEAR(pulses[0].amplitude, 1.0, 1e-6);
	CHECK_NEAR(pulses[0].seconds, 0.080, 1e-6);

	// Recentre and impact in one frame: still one pulse per hand
	vr_haptic_clear();
	vr_pad_t pad;
	vr_pad_sample_t s;
	vr_pad_init(&pad);
	memset(&s, 0, sizeof(s));
	vr_pad_update(&pad, &s, 600.0, record, NULL);
	s.view = true;
	vr_pad_update(&pad, &s, 600.0, record, NULL);
	vr_pad_update(&pad, &s, 601.0, record, NULL);
	vr_haptic_camera_shake(2.0f / 30.0f);
	CHECK_INT(flush(1.0f), 2);
	CHECK_NEAR(pulses[0].amplitude, 0.6, 1e-6); // system 0.6 beats a short shake's 0.415
	CHECK(vr_take_recentre_request());
	vr_haptic_clear();
}

static void test_clear_drops_queued_haptics(void) {
	// Focus loss: what was queued does not play late
	vr_haptic_ui_click();
	vr_haptic_camera_shake(1.0f);
	vr_haptic_clear();
	CHECK_INT(flush(1.0f), 0);
}

static void quiet(const char *line) {
	(void)line;
}

int main(void) {
	vr_log_set_sink(quiet);
	RUN(test_pad_range);
	RUN(test_buttons_map_to_game_codes);
	RUN(test_analog_becomes_split_axes);
	RUN(test_write_on_change);
	RUN(test_release_on_focus_loss);
	RUN(test_game_input_layer);
	RUN(test_system_short_press);
	RUN(test_system_hold_recentres_at_1s_and_3s);
	RUN(test_system_late_update_requests_once);
	RUN(test_system_focus_loss_cancels_the_press);
	RUN(test_system_never_binds_the_system_button);
	RUN(test_alias_rule);
	RUN(test_aliases_on_defaults);
	RUN(test_aliases_do_not_clobber_the_user);
	RUN(test_select_binding_does_not_clobber);
	RUN(test_short_press_goes_back_in_menus);
	RUN(test_haptic_event_table);
	RUN(test_haptic_shake_impact);
	RUN(test_clear_drops_queued_haptics);
	return SFVR_TEST_RESULT("vr_input");
}
