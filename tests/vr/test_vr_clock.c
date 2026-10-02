#include "sfvr_test.h"
#include "vr_clock.h"

static void test_first_frame_is_zero(void) {
	vr_clock_t c;
	vr_clock_reset(&c);
	CHECK_NEAR(vr_clock_step(&c, 5000000000LL, true, 0.1), 0.0, 1e-12);
	CHECK_NEAR(vr_clock_step(&c, 5013888889LL, true, 0.1), 0.013888889, 1e-9);
	CHECK_NEAR(vr_clock_step(&c, 5027777778LL, true, 0.1), 0.013888889, 1e-9);
}

static void test_clamp(void) {
	vr_clock_t c;
	vr_clock_reset(&c);
	vr_clock_step(&c, 1000000000LL, true, 0.1);
	CHECK_NEAR(vr_clock_step(&c, 3000000000LL, true, 0.1), 0.1, 1e-12);
	// the next frame measures from the clamped frame's own time, not the old one
	CHECK_NEAR(vr_clock_step(&c, 3013888889LL, true, 0.1), 0.013888889, 1e-9);
}

static void test_time_not_advancing(void) {
	vr_clock_t c;
	vr_clock_reset(&c);
	vr_clock_step(&c, 1000000000LL, true, 0.1);
	CHECK_NEAR(vr_clock_step(&c, 1000000000LL, true, 0.1), 0.0, 1e-12); // same time
	CHECK_NEAR(vr_clock_step(&c, 900000000LL, true, 0.1), 0.0, 1e-12); // backwards
	CHECK_NEAR(vr_clock_step(&c, 913888889LL, true, 0.1), 0.013888889, 1e-9);
}

static void test_frozen_keeps_time_base(void) {
	vr_clock_t c;
	vr_clock_reset(&c);
	vr_clock_step(&c, 1000000000LL, true, 0.1);
	// standby for ten seconds of display time, one frame at a time
	for (int i = 1; i <= 10; i++) {
		CHECK_NEAR(vr_clock_step(&c, 1000000000LL + i * 1000000000LL, false, 0.1), 0.0, 1e-12);
	}
	// the first frame after it is a normal one, not a 0.1 s jump
	CHECK_NEAR(vr_clock_step(&c, 11013888889LL, true, 0.1), 0.013888889, 1e-9);
}

static void test_reset(void) {
	vr_clock_t c;
	vr_clock_reset(&c);
	vr_clock_step(&c, 1000000000LL, true, 0.1);
	vr_clock_step(&c, 1013888889LL, true, 0.1);
	vr_clock_reset(&c);
	CHECK_NEAR(vr_clock_step(&c, 9000000000LL, true, 0.1), 0.0, 1e-12);
	// a zero-initialised clock works too
	vr_clock_t z = {0};
	CHECK_NEAR(vr_clock_step(&z, 100, true, 0.1), 0.0, 1e-12);
}

int main(void) {
	RUN(test_first_frame_is_zero);
	RUN(test_clamp);
	RUN(test_time_not_advancing);
	RUN(test_frozen_keeps_time_base);
	RUN(test_reset);
	return SFVR_TEST_RESULT("vr_clock");
}
