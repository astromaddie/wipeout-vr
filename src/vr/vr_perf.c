#include "vr_perf.h"

void vr_perf_init(vr_perf_t *p) {
	static const char *const stages[3] = { "record", "replay", "hud" };
	sfvr_perf_init(&p->perf, VR_PERF_WINDOW_S, stages, 3);
	p->has_last = false;
	p->last_display_time = 0;
}

bool vr_perf_frame(vr_perf_t *p, double now_s, int64_t display_time_ns, int64_t period_ns,
	float record_ms, float replay_ms, float hud_ms, const float eye_ms[2], char *line, size_t n
) {
	int missed = 0;
	if (p->has_last && period_ns > 0 && (display_time_ns - p->last_display_time) * 2 > period_ns * 3) {
		missed = 1;
	}
	p->last_display_time = display_time_ns;
	p->has_last = true;

	float stages[3] = { record_ms, replay_ms, hud_ms };
	sfvr_perf_add_frame(&p->perf, now_s, missed, stages, eye_ms, -1.0f);
	return sfvr_perf_poll(&p->perf, now_s, line, n) != 0;
}
