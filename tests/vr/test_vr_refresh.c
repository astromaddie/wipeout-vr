#include "sfvr_test.h"
#include "vr_refresh.h"

// A display clock and what a run of frames produced
typedef struct {
	int64_t t; // ns
	int held, missed, not_held, changed;
	vr_refresh_step_t last; // the last step that was not NONE
	vr_refresh_step_t report; // the NOT_HELD step
} run_t;

// Frames at `hz` of display time for `seconds`; the display period is `period_hz` (the rate in effect)
static void feed_at(vr_refresh_t *r, run_t *run, double hz, int period_hz, double seconds, bool worn) {
	int64_t step = (int64_t)(1e9 / hz);
	int64_t period = (int64_t)(1e9 / period_hz);
	int64_t end = run->t + (int64_t)(seconds * 1e9);
	while (run->t < end) {
		vr_refresh_step_t s;
		vr_refresh_frame(r, run->t, period, worn, &s);
		switch (s.event) {
			case VR_REFRESH_WINDOW_HELD: run->held++; run->last = s; break;
			case VR_REFRESH_WINDOW_MISSED: run->missed++; run->last = s; break;
			case VR_REFRESH_NOT_HELD: run->not_held++; run->report = run->last = s; break;
			case VR_REFRESH_RATE_CHANGED: run->changed++; break;
			default: break;
		}
		run->t += step;
	}
}

// The usual case: the app's frames come at the display rate, or slower
static void feed(vr_refresh_t *r, run_t *run, double hz, double seconds, bool worn) {
	feed_at(r, run, hz, 120, seconds, worn);
}

static const float RATES_FRAME[] = { 72, 90, 120 };
static const float RATES_ALL[] = { 144, 120, 90, 72 };

static vr_refresh_t make(int wanted) {
	vr_refresh_t r;
	vr_refresh_init(&r, wanted);
	return r;
}

static void test_offer_picks_the_closest(void) {
	vr_refresh_t r = make(120);
	const float shuffled[] = { 90, 144, 72, 120 };
	CHECK_INT(vr_refresh_offer(&r, shuffled, 4), 120);
	CHECK_INT(r.requested, 120);
	CHECK_INT(r.offered_count, 4);
	CHECK_INT(r.offered[0], 144); // sorted, highest first
	CHECK_INT(r.offered[3], 72);

	// 120 is not offered: the closest of the rest (90 is 30 away, 144 is 24 away)
	const float no120[] = { 72, 90, 144 };
	r = make(120);
	CHECK_INT(vr_refresh_offer(&r, no120, 3), 144);

	// a tie goes to the lower rate: 108 is 18 from both 90 and 126
	const float tie[] = { 90, 126 };
	r = make(108);
	CHECK_INT(vr_refresh_offer(&r, tie, 2), 90);

	// the Frame's per-app rate is the only one offered: that one, whatever was wanted
	const float only72[] = { 72 };
	r = make(120);
	CHECK_INT(vr_refresh_offer(&r, only72, 1), 72);
	const float only120[] = { 120 };
	r = make(90);
	CHECK_INT(vr_refresh_offer(&r, only120, 1), 120);

	// junk is skipped, duplicates collapse
	const float junk[] = { 0, -5, 90, 90.2f, 120 };
	r = make(120);
	CHECK_INT(vr_refresh_offer(&r, junk, 5), 120);
	CHECK_INT(r.offered_count, 2);

	// nothing offered, or nothing wanted
	r = make(120);
	CHECK_INT(vr_refresh_offer(&r, shuffled, 0), 0);
	r = make(0);
	CHECK_INT(vr_refresh_offer(&r, shuffled, 4), 0);
	CHECK_INT(r.requested, 0);
}

static void test_rate_from_period(void) {
	CHECK_INT(vr_refresh_rate_from_period(8333333), 120);
	CHECK_INT(vr_refresh_rate_from_period(11111111), 90);
	CHECK_INT(vr_refresh_rate_from_period(13888889), 72);
	CHECK_INT(vr_refresh_rate_from_period(6944444), 144);
	CHECK_INT(vr_refresh_rate_from_period(0), 0);
	CHECK_INT(vr_refresh_rate_from_period(-1), 0);
}

static void test_recommend(void) {
	CHECK_INT(vr_refresh_recommend(130), 144); // 144 needs 129.6
	CHECK_INT(vr_refresh_recommend(129), 120);
	CHECK_INT(vr_refresh_recommend(110), 120);
	CHECK_INT(vr_refresh_recommend(107.9), 90);
	CHECK_INT(vr_refresh_recommend(81), 90);
	CHECK_INT(vr_refresh_recommend(80.9), 72);
	CHECK_INT(vr_refresh_recommend(30), 72);
}

static void test_held_rate_says_nothing(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 120, 100, true);
	CHECK(run.held >= 9);
	CHECK_INT(run.missed + run.not_held, 0);
	CHECK_INT(run.changed, 1); // the first frame says what the display rate is
	CHECK_INT(r.rate, 120);
	CHECK_NEAR(run.last.fps, 120, 0.5);

	// exactly at 90%: held; just under: a miss
	r = make(120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	run = (run_t){0};
	feed(&r, &run, 108, 40, true);
	CHECK_INT(run.missed, 0);
	feed(&r, &run, 107, 40, true);
	CHECK(run.missed >= 1);
}

static void test_two_windows_recommend_once(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
	vr_refresh_offer(&r, RATES_FRAME, 3);

	// 100 fps at a 120 Hz display: below 108. One window is not enough
	feed(&r, &run, 100, 10.2, true);
	CHECK_INT(run.missed, 1);
	CHECK_INT(run.not_held, 0);
	CHECK_INT(r.misses, 1);
	CHECK_INT(run.last.event, VR_REFRESH_WINDOW_MISSED);

	// the second consecutive one recommends 90 (100 fps holds 90, not 120)
	feed(&r, &run, 100, 10, true);
	CHECK_INT(run.not_held, 1);
	CHECK_INT(run.report.rate, 120);
	CHECK_INT(run.report.recommend, 90);
	CHECK_NEAR(run.report.fps, 100, 1.0);
	CHECK_NEAR(run.report.prev_fps, 100, 1.0);
	CHECK_INT(r.rate, 120); // nothing changed: the app cannot change it

	// once per session, however long it stays bad
	feed(&r, &run, 60, 300, true);
	CHECK_INT(run.not_held, 1);
	CHECK_INT(run.missed, 1);
}

static void test_recommendation_follows_the_measured_rate(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 70, 22, true); // 70 fps: only 72 is within reach (70 < 81)
	CHECK_INT(run.not_held, 1);
	CHECK_INT(run.report.recommend, 72);

	// a 90 Hz display at 70 fps: 72 is lower, so recommend it
	r = make(90);
	run = (run_t){0};
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed_at(&r, &run, 70, 90, 22, true);
	CHECK_INT(run.not_held, 1);
	CHECK_INT(run.report.rate, 90);
	CHECK_INT(run.report.recommend, 72);

	// a 72 Hz display that cannot hold 72: no lower rate to recommend
	r = make(72);
	run = (run_t){0};
	feed_at(&r, &run, 40, 72, 22, true);
	CHECK_INT(run.not_held, 1);
	CHECK_INT(run.report.rate, 72);
	CHECK_INT(run.report.recommend, 0);

	// at 90 Hz and 85 fps (above 81): held, nothing said
	r = make(90);
	run = (run_t){0};
	feed_at(&r, &run, 85, 90, 40, true);
	CHECK_INT(run.not_held + run.missed, 0);
}

static void test_the_period_not_the_list_is_the_rate(void) {
	// the extension may list only 90 while the compositor runs at 120 (lesson 79): the 90% is of the period's rate
	vr_refresh_t r = make(120);
	run_t run = {0};
	const float lists_90[] = { 90 };
	CHECK_INT(vr_refresh_offer(&r, lists_90, 1), 90);
	feed_at(&r, &run, 100, 120, 22, true);
	CHECK_INT(r.rate, 120);
	CHECK_INT(run.not_held, 1);
	CHECK_INT(run.report.rate, 120);

	// the display rate changing restarts the count (the user changed it, or a mode switched)
	r = make(120);
	run = (run_t){0};
	feed_at(&r, &run, 100, 120, 10.2, true);
	CHECK_INT(r.misses, 1);
	feed_at(&r, &run, 100, 90, 10.2, true);
	CHECK_INT(r.rate, 90);
	CHECK_INT(run.changed, 2);
	CHECK_INT(run.not_held, 0);

	// a frame without a period is not judged until one arrives
	r = make(120);
	run = (run_t){0};
	for (int i = 0; i < 2000; i++) {
		vr_refresh_step_t s;
		vr_refresh_frame(&r, (int64_t)i * 8333333, 0, true, &s);
		CHECK_INT(s.event, VR_REFRESH_NONE);
	}
}

static void test_only_while_worn(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
	vr_refresh_offer(&r, RATES_ALL, 4);

	// not worn (standby, no focus, force_render): 5 minutes of bad frames count for nothing
	feed(&r, &run, 40, 300, false);
	CHECK_INT(run.held + run.missed + run.not_held, 0);

	// one bad worn window, the headset comes off, another bad one: not consecutive
	feed(&r, &run, 100, 10.2, true);
	CHECK_INT(run.missed, 1);
	feed(&r, &run, 100, 5, false);
	feed(&r, &run, 100, 10.2, true);
	CHECK_INT(run.missed, 2);
	CHECK_INT(run.not_held, 0);
	CHECK_INT(r.misses, 1);

	// a window interrupted by taking the headset off does not count even if both halves were bad
	r = make(120);
	vr_refresh_offer(&r, RATES_ALL, 4);
	run = (run_t){0};
	feed(&r, &run, 100, 6, true);
	feed(&r, &run, 100, 3, false);
	feed(&r, &run, 100, 6, true);
	CHECK_INT(run.missed + run.not_held, 0);
}

static void test_a_good_window_resets_the_run(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 100, 10.2, true); // miss
	feed(&r, &run, 120, 10, true); // held
	feed(&r, &run, 100, 10, true); // miss, but not consecutive
	CHECK_INT(run.not_held, 0);
	CHECK_INT(r.misses, 1);
	feed(&r, &run, 100, 10, true);
	CHECK_INT(run.not_held, 1);
}

static void test_stalls_are_not_judged(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
	vr_refresh_offer(&r, RATES_ALL, 4);
	feed(&r, &run, 100, 10.2, true); // one miss
	run.t += INT64_C(8000000000); // a load that blocked the loop for 8 s
	feed(&r, &run, 120, 10.2, true);
	CHECK_INT(run.not_held, 0);
	CHECK_INT(r.misses, 0);
}

static void test_fps_is_frames_over_display_time(void) {
	vr_refresh_t r = make(120);
	run_t run = {0};
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
		vr_refresh_frame(&r, run.t, step, true, &s);
		if (s.event == VR_REFRESH_WINDOW_MISSED || s.event == VR_REFRESH_NOT_HELD) {
			n++;
			CHECK_NEAR(s.fps, 90, 1.5);
		}
		run.t += step;
	}
	CHECK(n >= 1);
}

int main(void) {
	RUN(test_offer_picks_the_closest);
	RUN(test_rate_from_period);
	RUN(test_recommend);
	RUN(test_held_rate_says_nothing);
	RUN(test_two_windows_recommend_once);
	RUN(test_recommendation_follows_the_measured_rate);
	RUN(test_the_period_not_the_list_is_the_rate);
	RUN(test_only_while_worn);
	RUN(test_a_good_window_resets_the_run);
	RUN(test_stalls_are_not_judged);
	RUN(test_fps_is_frames_over_display_time);
	return SFVR_TEST_RESULT("vr_refresh");
}
