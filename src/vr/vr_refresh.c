#include <string.h>

#include "vr_refresh.h"

void vr_refresh_init(vr_refresh_t *r, int wanted) {
	memset(r, 0, sizeof(*r));
	r->wanted = wanted;
}

static void window_reset(vr_refresh_t *r) {
	r->window_open = false;
	r->window_frames = 0;
}

int vr_refresh_offer(vr_refresh_t *r, const float *rates, uint32_t count) {
	r->offered_count = 0;
	r->requested = 0;

	for (uint32_t i = 0; i < count && i < VR_REFRESH_MAX_RATES; i++) {
		int rate = (int)(rates[i] + 0.5f);
		if (!(rates[i] > 0) || rate <= 0) {
			continue;
		}
		bool dup = false;
		for (int k = 0; k < r->offered_count; k++) {
			dup |= r->offered[k] == rate;
		}
		if (dup) {
			continue;
		}
		int at = r->offered_count++;
		while (at > 0 && r->offered[at - 1] < rate) {
			r->offered[at] = r->offered[at - 1];
			at--;
		}
		r->offered[at] = rate;
	}
	if (r->offered_count == 0 || r->wanted <= 0) {
		return 0;
	}

	int best = 0, best_distance = 0;
	for (int i = r->offered_count - 1; i >= 0; i--) { // lowest first, so a tie keeps the lower rate
		int d = r->offered[i] > r->wanted ? r->offered[i] - r->wanted : r->wanted - r->offered[i];
		if (best == 0 || d < best_distance) {
			best = r->offered[i];
			best_distance = d;
		}
	}
	r->requested = best;
	return best;
}

int vr_refresh_rate_from_period(int64_t period_ns) {
	if (period_ns <= 0) {
		return 0;
	}
	return (int)((1e9 / (double)period_ns) + 0.5);
}

int vr_refresh_recommend(double fps) {
	static const int standard[] = { 144, 120, 90 };
	for (size_t i = 0; i < sizeof(standard) / sizeof(standard[0]); i++) {
		if (fps >= (double)standard[i] * VR_REFRESH_HELD_RATIO) {
			return standard[i];
		}
	}
	return 72;
}

void vr_refresh_frame(vr_refresh_t *r, int64_t display_time_ns, int64_t period_ns, bool worn, vr_refresh_step_t *out) {
	memset(out, 0, sizeof(*out));

	int rate = vr_refresh_rate_from_period(period_ns);
	if (rate > 0 && rate != r->rate) {
		r->rate = rate;
		r->misses = 0;
		window_reset(r);
		out->event = VR_REFRESH_RATE_CHANGED;
	}
	out->rate = r->rate;

	bool had_window = r->window_open;
	int64_t last = r->last_ns;
	r->last_ns = display_time_ns;

	if (r->reported || r->rate <= 0) {
		return;
	}
	if (!worn) {
		// Off the head, in standby or without focus: the window does not count, and the
		// windows before it are not consecutive with the ones after
		window_reset(r);
		r->misses = 0;
		return;
	}
	if (had_window && display_time_ns - last > VR_REFRESH_GAP_NS) {
		// A stall (a load, a pause): not what the rate is about; start over
		window_reset(r);
		r->misses = 0;
	}
	if (!r->window_open) {
		r->window_open = true;
		r->window_start_ns = display_time_ns;
		r->window_frames = 1;
		return;
	}
	if (display_time_ns - r->window_start_ns < VR_REFRESH_WINDOW_NS) {
		r->window_frames++;
		return;
	}

	// The window [start, now) is complete: window_frames frames in that time
	double seconds = (double)(display_time_ns - r->window_start_ns) * 1e-9;
	double fps = (double)r->window_frames / seconds;
	r->window_start_ns = display_time_ns;
	r->window_frames = 1;
	out->fps = fps;

	if (fps >= (double)r->rate * VR_REFRESH_HELD_RATIO) {
		r->misses = 0;
		out->event = VR_REFRESH_WINDOW_HELD;
		return;
	}

	r->misses++;
	out->misses = r->misses;
	if (r->misses < VR_REFRESH_MISSES) {
		r->prev_fps = fps;
		out->event = VR_REFRESH_WINDOW_MISSED;
		return;
	}
	out->event = VR_REFRESH_NOT_HELD;
	out->prev_fps = r->prev_fps;
	double mean = (r->prev_fps + fps) * 0.5;
	int recommend = vr_refresh_recommend(mean);
	out->recommend = recommend < r->rate ? recommend : 0;
	r->reported = true; // once per session
	r->misses = 0;
	window_reset(r);
}
