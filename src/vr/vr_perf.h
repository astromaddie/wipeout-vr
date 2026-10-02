#ifndef VR_PERF_H
#define VR_PERF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sfvr/sfvr_perf.h"

// The `[vr-perf]` line (standard.md section 8), fed per frame. Stages are the
// CPU cost of recording the game's draw calls ("record"), of replaying both
// eyes ("replay") and of the HUD pass ("hud"); eye= is the per-eye replay.
// All are CPU submission times: a GL command takes the time to issue, not to
// execute, and nothing here reads back from the GPU.

#define VR_PERF_WINDOW_S 10.0

typedef struct {
	sfvr_perf perf;
	int64_t last_display_time;
	bool has_last;
} vr_perf_t;

void vr_perf_init(vr_perf_t *p);

// One frame. A frame is missed when the display time advanced by more than 1.5
// display periods since the previous frame. Returns true and fills `line` when the
// 10 s window has elapsed. now_s is a monotonic wall clock in seconds.
bool vr_perf_frame(vr_perf_t *p, double now_s, int64_t display_time_ns, int64_t period_ns,
	float record_ms, float replay_ms, float hud_ms, const float eye_ms[2], char *line, size_t n);

#endif
