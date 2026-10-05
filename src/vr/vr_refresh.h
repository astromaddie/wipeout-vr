#ifndef VR_REFRESH_H
#define VR_REFRESH_H

#include <stdbool.h>
#include <stdint.h>

// The display refresh rate (standard.md section 9). Pure: no runtime, no clock but
// the one passed in, so it is unit-tested without OpenXR.
//
// On the Frame an app cannot change the refresh rate: SteamVR offers only the title's
// per-app rate, so the app asks once, for the offered rate closest to `wanted` (120 by
// default), and does not fall back (a ladder could not take effect). What it can do is
// notice that the frame rate is not held and say which per-app rate to set.
//
// The rate in effect comes from the runtime's predictedDisplayPeriod, which is
// trusted over the offered list. While the headset is worn, frames are counted in
// 10 s windows of display time. Two consecutive windows below 90% of the display rate
// produce one recommendation, once per session: the highest standard rate those windows
// would have held (or the lowest, 72). It changes nothing.
//
// A window that was not worn throughout does not count and breaks the run of
// consecutive ones; a stall of more than 5 s between frames (a load) restarts the count.

#define VR_REFRESH_MAX_RATES 16
#define VR_REFRESH_WINDOW_NS INT64_C(10000000000)
#define VR_REFRESH_GAP_NS INT64_C(5000000000)
#define VR_REFRESH_HELD_RATIO 0.9
#define VR_REFRESH_MISSES 2 // consecutive windows below the ratio that trigger the recommendation

typedef struct {
	int offered[VR_REFRESH_MAX_RATES]; // whole Hz, highest first
	int offered_count;
	int wanted;
	int requested; // the rate vr_refresh_offer() chose; 0 when none
	int rate; // the display rate in effect, from the period; 0 until a frame says
	bool reported; // the recommendation was made: nothing more is judged
	int misses; // consecutive windows below the ratio
	double prev_fps; // the first of them, for the log
	// the window being counted
	bool window_open;
	int64_t window_start_ns;
	int64_t last_ns;
	uint32_t window_frames;
} vr_refresh_t;

typedef enum {
	VR_REFRESH_NONE, // nothing happened this frame
	VR_REFRESH_RATE_CHANGED, // the display rate in effect is now `rate` (also the first time it is known)
	VR_REFRESH_WINDOW_HELD, // a worn window ended at or above the ratio
	VR_REFRESH_WINDOW_MISSED, // a worn window ended below it (misses < VR_REFRESH_MISSES)
	VR_REFRESH_NOT_HELD // the second one: recommend `recommend` (once per session)
} vr_refresh_event_t;

typedef struct {
	vr_refresh_event_t event;
	int rate; // the display rate in effect
	double fps; // the window's frames per second (WINDOW_* and NOT_HELD)
	double prev_fps; // NOT_HELD: the window before
	int misses;
	int recommend; // NOT_HELD: the per-app rate to set; 0 when the display is already at the lowest (72)
} vr_refresh_step_t;

// wanted <= 0: nothing is requested.
void vr_refresh_init(vr_refresh_t *r, int wanted);

// The runtime's offered rates (Hz, any order, junk skipped). Returns the one rate to
// request, once: the offered rate closest to `wanted`, the lower one on a tie; 0 when
// none are offered or nothing is wanted.
int vr_refresh_offer(vr_refresh_t *r, const float *rates, uint32_t count);

// The display rate for a predictedDisplayPeriod, whole Hz; 0 for a bad period.
int vr_refresh_rate_from_period(int64_t period_ns);

// The per-app rate to recommend when `fps` is what the app managed: the highest of
// 144, 120, 90, 72 that fps would hold (at 90% or better), else 72.
int vr_refresh_recommend(double fps);

// One begun frame at its predicted display time and period. `worn`: the session is
// focused and shouldRender is true, and force_render is off. Fills `out` (always).
void vr_refresh_frame(vr_refresh_t *r, int64_t display_time_ns, int64_t period_ns, bool worn, vr_refresh_step_t *out);

#endif
