#include <stdio.h>
#include <string.h>

#include "vr_perf.h"

void vr_perf_init(vr_perf_t *p) {
	static const char *const stages[3] = { "record", "replay", "hud" };
	sfvr_perf_init(&p->perf, VR_PERF_WINDOW_S, stages, 3);
	p->has_last = false;
	p->last_display_time = 0;
	p->gpu_stereo = p->gpu_mono = 0;
	memset(p->gpu_eye_sum, 0, sizeof(p->gpu_eye_sum));
	p->gpu_face_sum = p->gpu_hud_sum = p->gpu_mono_sum = p->gpu_total_max = 0;
}

bool vr_perf_frame(vr_perf_t *p, double now_s, int64_t display_time_ns, int64_t period_ns,
	float record_ms, float replay_ms, float hud_ms, const float eye_ms[2], float gpu_ms, char *line, size_t n
) {
	int missed = 0;
	if (p->has_last && period_ns > 0 && (display_time_ns - p->last_display_time) * 2 > period_ns * 3) {
		missed = 1;
	}
	p->last_display_time = display_time_ns;
	p->has_last = true;

	float stages[3] = { record_ms, replay_ms, hud_ms };
	sfvr_perf_add_frame(&p->perf, now_s, missed, stages, eye_ms, gpu_ms);
	return sfvr_perf_poll(&p->perf, now_s, line, n) != 0;
}

void vr_perf_gpu_sample(vr_perf_t *p, bool stereo, float face_ms, const float eye_ms[2], float hud_ms, float mono_ms, float total_ms) {
	if (stereo) {
		p->gpu_stereo++;
		p->gpu_eye_sum[0] += eye_ms[0];
		p->gpu_eye_sum[1] += eye_ms[1];
		p->gpu_face_sum += face_ms;
		p->gpu_hud_sum += hud_ms;
	}
	else {
		p->gpu_mono++;
		p->gpu_mono_sum += mono_ms;
	}
	if (total_ms > p->gpu_total_max) {
		p->gpu_total_max = total_ms;
	}
}

void vr_perf_gpu_line(vr_perf_t *p, char *line, size_t n) {
	if (n == 0) {
		return;
	}
	line[0] = '\0';
	unsigned frames = p->gpu_stereo + p->gpu_mono;
	if (frames > 0) {
		double s = p->gpu_stereo ? 1.0 / p->gpu_stereo : 0.0;
		double m = p->gpu_mono ? 1.0 / p->gpu_mono : 0.0;
		snprintf(line, n, "[vr-perf] gpu eye=%.2f/%.2fms face=%.2f hud=%.2f mono=%.2f max=%.2fms n=%u",
			p->gpu_eye_sum[0] * s, p->gpu_eye_sum[1] * s, p->gpu_face_sum * s, p->gpu_hud_sum * s,
			p->gpu_mono_sum * m, p->gpu_total_max, frames);
	}
	p->gpu_stereo = p->gpu_mono = 0;
	memset(p->gpu_eye_sum, 0, sizeof(p->gpu_eye_sum));
	p->gpu_face_sum = p->gpu_hud_sum = p->gpu_mono_sum = p->gpu_total_max = 0;
}
