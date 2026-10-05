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
	r->active = false;
	r->rate = 0;
	r->misses = 0;
	window_reset(r);

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
		// insertion, highest first
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

	int chosen = r->offered[r->offered_count - 1]; // the lowest, when all are above `wanted`
	for (int i = 0; i < r->offered_count; i++) {
		if (r->offered[i] <= r->wanted) {
			chosen = r->offered[i];
			break;
		}
	}
	r->rate = chosen;
	r->active = r->offered_count > 1;
	return chosen;
}

void vr_refresh_adopt(vr_refresh_t *r, int rate) {
	if (rate > 0 && r->rate > 0 && rate != r->rate) {
		r->rate = rate;
		r->misses = 0;
		window_reset(r);
	}
}

void vr_refresh_give_up(vr_refresh_t *r) {
	r->active = false;
	r->misses = 0;
	window_reset(r);
}

int vr_refresh_next_lower(const vr_refresh_t *r) {
	for (int i = 0; i < r->offered_count; i++) {
		if (r->offered[i] < r->rate && r->offered[i] >= VR_REFRESH_FLOOR) {
			return r->offered[i];
		}
	}
	return 0;
}

void vr_refresh_frame(vr_refresh_t *r, int64_t display_time_ns, bool worn, vr_refresh_step_t *out) {
	memset(out, 0, sizeof(*out));
	out->from = r->rate;

	int64_t last = r->last_ns;
	bool had_last = r->window_open;
	r->last_ns = display_time_ns;

	// At the lowest rate there is nothing to fall back to, so nothing to judge
	if (!r->active || vr_refresh_next_lower(r) == 0) {
		window_reset(r);
		r->misses = 0;
		return;
	}
	if (!worn) {
		// Off the head, in standby or without focus: the window does not count, and the
		// windows before it are not consecutive with the ones after
		window_reset(r);
		r->misses = 0;
		return;
	}
	if (had_last && display_time_ns - last > VR_REFRESH_GAP_NS) {
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
	out->fps = fps;
	r->window_start_ns = display_time_ns;
	r->window_frames = 1;

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
	out->event = VR_REFRESH_FALL_BACK;
	out->prev_fps = r->prev_fps;
	out->to = vr_refresh_next_lower(r);
	r->rate = out->to;
	r->misses = 0;
	r->prev_fps = 0;
	window_reset(r);
}
