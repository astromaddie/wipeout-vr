#include "vr_clock.h"

void vr_clock_reset(vr_clock_t *c) {
	c->last_time = 0;
	c->has_last = false;
}

double vr_clock_step(vr_clock_t *c, int64_t predicted_display_time, bool sim_active, double max_dt) {
	double dt = 0;
	if (c->has_last && predicted_display_time > c->last_time) {
		dt = (double)(predicted_display_time - c->last_time) * 1e-9;
		if (dt > max_dt) {
			dt = max_dt;
		}
	}
	c->last_time = predicted_display_time;
	c->has_last = true;
	return sim_active ? dt : 0;
}
