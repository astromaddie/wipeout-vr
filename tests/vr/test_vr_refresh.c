#include "sfvr_test.h"
#include "vr_refresh.h"

// A display clock and what a run of frames produced
typedef struct {
	int64_t t; // ns
	int held, missed, fell;
	vr_refresh_step_t last_fall; // the FALL_BACK step
	vr_refresh_step_t last; // the last step that was not NONE
} run_t;

// Frames at `hz` of display time for `seconds`, as seen by the machine
static void feed(vr_refresh_t *r, run_t *run, double hz, double seconds, bool worn) {
	int64_t step = (int64_t)(1e9 / hz);
	int64_t end = run->t + (int64_t)(seconds * 1e9);
	while (run->t < end) {
		vr_refresh_step_t s;
		vr_refresh_frame(r, run->t, worn, &s);
		switch (s.event) {
			case VR_REFRESH_WINDOW_HELD: run->held++; run->last = s; break;
			case VR_REFRESH_WINDOW_MISSED: run->missed++; run->last = s; break;
			case VR_REFRESH_FALL_BACK: run->fell++; run->last_fall = run->last = s; break;
			default: break;
		}
		run->t += step;
	}
}

static const float RATES_FRAME[] = { 72, 90, 120 };
static const float RATES_ALL[] = { 144, 120, 90, 72 };

static void test_offer_picks_best_at_or_below(void) {
	vr_refresh_t r;
	const float shuffled[] = { 90, 144, 72, 120 };

	vr_refresh_init(&r, 120);
	CHECK_INT(vr_refresh_offer(&r, shuffled, 4), 120);
	CHECK_INT(r.rate, 120);
	CHECK(r.active);
	CHECK_INT(r.offered_count, 4);
	CHECK_INT(r.offered[0], 144); // sorted, highest first
	CHECK_INT(r.offered[3], 72);

	// 120 is not offered: the best below it
	const float no120[] = { 72, 90, 144 };
	vr_refresh_init(&r, 120);
	CHECK_INT(vr_refresh_offer(&r, no120, 3), 90);

	// only rates above the wanted one: the lowest of them
	const float high[] = { 120, 144 };
	vr_refresh_init(&r, 90);
	CHECK_INT(vr_refresh_offer(&r, high, 2), 120);

	// 100 wanted: the best at or below
	vr_refresh_init(&r, 100);
	CHECK_INT(vr_refresh_offer(&r, shuffled, 4), 90);

	// junk is skipped, duplicates collapse
	const float junk[] = { 0, -5, 90, 90.2f, 120 };
	vr_refresh_init(&r, 120);
	CHECK_INT(vr_refresh_offer(&r, junk, 5), 120);
	CHECK_INT(r.offered_count, 2);

	// nothing offered, or nothing wanted
	vr_refresh_init(&r, 120);
	CHECK_INT(vr_refresh_offer(&r, shuffled, 0), 0);
	CHECK(!r.active);
	vr_refresh_init(&r, 0);
	CHECK_INT(vr_refresh_offer(&r, shuffled, 4), 0);
	CHECK(!r.active);
}

static void test_single_rate_stays(void) {
	vr_refresh_t r;
	run_t run = {0};
	const float one[] = { 72 };

	// what the Frame offered an app with no per-app entry
	vr_refresh_init(&r, 120);
	CHECK_INT(vr_refresh_offer(&r, one, 1), 72);
	CHECK(!r.active);
	feed(&r, &run, 40, 120, true); // hopeless frame rate, no events at all
	CHECK_INT(run.held + run.missed + run.fell, 0);
	CHECK_INT(r.rate, 72);

	// and an app set to 120 in SteamVR sees only 120
	const float only120[] = { 120 };
	vr_refresh_init(&r, 120);
	run = (run_t){0};
	CHECK_INT(vr_refresh_offer(&r, only120, 1), 120);
	feed(&r, &run, 60, 120, true);
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.rate, 120);
}

static void test_held_rate_never_falls_back(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 120, 100, true);
	CHECK(run.held >= 9);
	CHECK_INT(run.missed, 0);
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.rate, 120);
	CHECK_NEAR(run.last.fps, 120, 0.5);

	// exactly at 90%: held; just under: a miss
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	run = (run_t){0};
	feed(&r, &run, 108, 40, true);
	CHECK_INT(run.missed, 0);
	feed(&r, &run, 107, 40, true);
	CHECK(run.missed >= 1);
}

static void test_two_windows_fall_back_120_90_72(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	CHECK_INT(vr_refresh_offer(&r, RATES_FRAME, 3), 120);

	// 100 fps while 120 is asked for: below 108. One window is not enough
	feed(&r, &run, 100, 10.2, true);
	CHECK_INT(run.missed, 1);
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.misses, 1);
	CHECK_INT(run.last.event, VR_REFRESH_WINDOW_MISSED);

	// the second consecutive one falls back to 90
	feed(&r, &run, 100, 10, true);
	CHECK_INT(run.fell, 1);
	CHECK_INT(run.last_fall.from, 120);
	CHECK_INT(run.last_fall.to, 90);
	CHECK_NEAR(run.last_fall.fps, 100, 1.0);
	CHECK_NEAR(run.last_fall.prev_fps, 100, 1.0);
	CHECK_INT(r.rate, 90);
	CHECK_INT(r.misses, 0);

	// 90 is held at 88 fps (>= 81): stays
	run = (run_t){ .t = run.t };
	feed(&r, &run, 88, 60, true);
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.rate, 90);

	// 70 fps is below 81: two windows to 72, and it takes the fall-back's time to be judged anew
	run = (run_t){ .t = run.t };
	feed(&r, &run, 70, 10.2, true);
	CHECK_INT(run.fell, 0);
	feed(&r, &run, 70, 10, true);
	CHECK_INT(run.fell, 1);
	CHECK_INT(run.last_fall.from, 90);
	CHECK_INT(run.last_fall.to, 72);
	CHECK_INT(r.rate, 72);

	// 72 is the floor: nothing more is judged, however bad
	run = (run_t){ .t = run.t };
	feed(&r, &run, 30, 120, true);
	CHECK_INT(run.held + run.missed + run.fell, 0);
	CHECK_INT(r.rate, 72);
}

static void test_fall_back_only_to_offered_rates(void) {
	vr_refresh_t r;
	run_t run = {0};

	// 120 wanted, 90 not offered: 120 -> 72
	const float no90[] = { 72, 120 };
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, no90, 2);
	feed(&r, &run, 60, 25, true);
	CHECK_INT(run.fell, 1);
	CHECK_INT(run.last_fall.to, 72);

	// 144 wanted: 144 -> 120 -> 90 -> 72 in turn
	vr_refresh_init(&r, 144);
	vr_refresh_offer(&r, RATES_ALL, 4);
	run = (run_t){0};
	int expect[] = { 120, 90, 72 };
	double fps[] = { 100, 80, 60 };
	for (int i = 0; i < 3; i++) {
		int before = run.fell;
		feed(&r, &run, fps[i], 22, true);
		CHECK_INT(run.fell, before + 1);
		CHECK_INT(r.rate, expect[i]);
	}

	// rates below the floor are not used: 72 and 60 offered, wanted 72 -> nothing to judge
	const float low[] = { 60, 72 };
	vr_refresh_init(&r, 72);
	run = (run_t){0};
	CHECK_INT(vr_refresh_offer(&r, low, 2), 72);
	feed(&r, &run, 30, 60, true);
	CHECK_INT(run.fell, 0);
	CHECK_INT(run.missed, 0);
}

static void test_only_while_worn(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);

	// not worn (standby, no focus, force_render): 5 minutes of bad frames count for nothing
	feed(&r, &run, 40, 300, false);
	CHECK_INT(run.held + run.missed + run.fell, 0);
	CHECK_INT(r.rate, 120);

	// one bad worn window, then the headset comes off for a while, then another bad one:
	// they are not consecutive, so no fall back
	feed(&r, &run, 100, 10.2, true);
	CHECK_INT(run.missed, 1);
	feed(&r, &run, 100, 5, false);
	feed(&r, &run, 100, 10.2, true);
	CHECK_INT(run.missed, 2);
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.misses, 1);

	// a window that was interrupted by taking the headset off does not count even if both halves were bad
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	run = (run_t){0};
	feed(&r, &run, 100, 6, true);
	feed(&r, &run, 100, 3, false);
	feed(&r, &run, 100, 6, true);
	CHECK_INT(run.missed + run.fell, 0);
}

static void test_a_good_window_between_resets_the_run(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 100, 10.2, true); // miss
	feed(&r, &run, 120, 10, true); // held
	feed(&r, &run, 100, 10, true); // miss, but not consecutive
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.misses, 1);
	feed(&r, &run, 100, 10, true);
	CHECK_INT(run.fell, 1);
}

static void test_stalls_are_not_judged(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 100, 10.2, true); // one miss
	run.t += INT64_C(8000000000); // a load that blocked the loop for 8 s
	feed(&r, &run, 120, 10.2, true);
	CHECK_INT(run.fell, 0);
	CHECK_INT(r.misses, 0);
}

static void test_runtime_reports_and_refusals(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);

	// the runtime confirming what we asked for changes nothing
	feed(&r, &run, 100, 10.2, true);
	vr_refresh_adopt(&r, 120);
	CHECK_INT(r.misses, 1);

	// the user (or the system) moved the rate: judge against that, from scratch
	vr_refresh_adopt(&r, 90);
	CHECK_INT(r.rate, 90);
	CHECK_INT(r.misses, 0);
	run = (run_t){ .t = run.t };
	feed(&r, &run, 88, 30, true);
	CHECK_INT(run.missed + run.fell, 0);
	vr_refresh_adopt(&r, 0); // nonsense is ignored
	CHECK_INT(r.rate, 90);

	// a refused request: stop trying
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	run = (run_t){0};
	feed(&r, &run, 60, 22, true);
	CHECK_INT(run.fell, 1);
	vr_refresh_give_up(&r);
	CHECK(!r.active);
	run = (run_t){ .t = run.t };
	feed(&r, &run, 20, 60, true);
	CHECK_INT(run.fell + run.missed, 0);
}

static void test_fps_is_frames_over_display_time(void) {
	vr_refresh_t r;
	run_t run = {0};
	vr_refresh_init(&r, 120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 118, 10.5, true);
	CHECK_INT(run.held, 1);
	CHECK_NEAR(run.last.fps, 118, 0.2);
	// frames that the app dropped show as fewer frames in the same display time
	run = (run_t){ .t = run.t };
	int64_t step = (int64_t)(1e9 / 120);
	int n = 0;
	for (int i = 0; i < 1300; i++) {
		if (i % 4 == 3) {
			run.t += step;
			continue; // one frame in four never begun
		}
		vr_refresh_step_t s;
		vr_refresh_frame(&r, run.t, true, &s);
		if (s.event == VR_REFRESH_WINDOW_MISSED || s.event == VR_REFRESH_FALL_BACK) {
			n++;
			CHECK_NEAR(s.fps, 90, 1.5);
		}
		run.t += step;
	}
	CHECK(n >= 1);
}

int main(void) {
	RUN(test_offer_picks_best_at_or_below);
	RUN(test_single_rate_stays);
	RUN(test_held_rate_never_falls_back);
	RUN(test_two_windows_fall_back_120_90_72);
	RUN(test_fall_back_only_to_offered_rates);
	RUN(test_only_while_worn);
	RUN(test_a_good_window_between_resets_the_run);
	RUN(test_stalls_are_not_judged);
	RUN(test_runtime_reports_and_refusals);
	RUN(test_fps_is_frames_over_display_time);
	return SFVR_TEST_RESULT("vr_refresh");
}
