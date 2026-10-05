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
// execute. GPU time (vr.timing_gpu) comes from timestamp queries that the
// renderer reads a few frames late (vr_render.h, vr_render_gpu); it fills the
// line's gpu= field with the sum of the passes, and a second line gives each pass.

#define VR_PERF_WINDOW_S 10.0

typedef struct {
	sfvr_perf perf;
	int64_t last_display_time;
	bool has_last;
	// GPU time per pass over the window; stereo and mono frames are averaged apart
	unsigned gpu_stereo, gpu_mono;
	double gpu_eye_sum[2], gpu_face_sum, gpu_hud_sum, gpu_mono_sum;
	double gpu_total_max;
} vr_perf_t;

void vr_perf_init(vr_perf_t *p);

// One frame. A frame is missed when the display time advanced by more than 1.5
// display periods since the previous frame. gpu_ms is the mean GPU time of the
// frames whose queries finished this frame, or negative when none did (the line's
// gpu= then averages only the frames that had one). Returns true and fills `line`
// when the 10 s window has elapsed. now_s is a monotonic wall clock in seconds.
bool vr_perf_frame(vr_perf_t *p, double now_s, int64_t display_time_ns, int64_t period_ns,
	float record_ms, float replay_ms, float hud_ms, const float eye_ms[2], float gpu_ms, char *line, size_t n);

// One finished GPU frame (a stereo frame has the eyes, the faces and the HUD; a mono
// frame only the panel). Call for each, whenever it is read.
void vr_perf_gpu_sample(vr_perf_t *p, bool stereo, float face_ms, const float eye_ms[2], float hud_ms, float mono_ms, float total_ms);

// After vr_perf_frame() returned true: the per-pass GPU line for the window, or an
// empty string when no GPU frame finished in it, then starts the next window.
//   [vr-perf] gpu eye=3.21/3.30ms face=0.05 hud=0.18 mono=0.00 max=9.50ms n=840
// eye, face and hud average the stereo frames, mono the mono ones, max is the worst
// frame's total, n the number of frames.
void vr_perf_gpu_line(vr_perf_t *p, char *line, size_t n);

#endif
