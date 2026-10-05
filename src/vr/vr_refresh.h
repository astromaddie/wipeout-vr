#ifndef VR_REFRESH_H
#define VR_REFRESH_H

#include <stdbool.h>
#include <stdint.h>

// The display refresh-rate fallback (standard.md section 9). Pure: no runtime, no
// clock but the one passed in, so it is unit-tested without OpenXR.
//
// The port asks for `wanted` Hz (120 by default). The runtime offers only some
// rates, so the first request is the best offered rate at or below `wanted`. While
// the headset is worn, the machine counts frames in 10 s windows of display time. Two
// consecutive windows below 90% of the rate in effect fall back to the next lower
// offered rate, never below 72 Hz (120 -> 90 -> 72). A window that was not worn
// throughout does not count and breaks the run of consecutive ones.
//
// If the runtime offers a single rate there is nothing to fall back to: the machine
// is inactive and the caller logs that once.

#define VR_REFRESH_MAX_RATES 16
#define VR_REFRESH_WINDOW_NS INT64_C(10000000000)
#define VR_REFRESH_GAP_NS INT64_C(5000000000) // a longer pause between frames restarts the window
#define VR_REFRESH_HELD_RATIO 0.9
#define VR_REFRESH_MISSES 2 // consecutive windows below the ratio that trigger a fall back
#define VR_REFRESH_FLOOR 72 // the lowest rate it falls back to

typedef struct {
	int offered[VR_REFRESH_MAX_RATES]; // whole Hz, highest first
	int offered_count;
	int wanted;
	int rate; // the rate believed to be in effect; 0 before the first offer
	bool active; // frames are judged, and a fall back may happen
	int misses; // consecutive windows below the ratio
	double prev_fps; // the first of those windows, for the log
	// the window being counted
	bool window_open;
	int64_t window_start_ns;
	int64_t last_ns;
	uint32_t window_frames;
} vr_refresh_t;

typedef enum {
	VR_REFRESH_NONE, // nothing happened this frame
	VR_REFRESH_WINDOW_HELD, // a worn window ended at or above the ratio
	VR_REFRESH_WINDOW_MISSED, // a worn window ended below it (misses < VR_REFRESH_MISSES)
	VR_REFRESH_FALL_BACK // the second one: ask the runtime for `to`
} vr_refresh_event_t;

typedef struct {
	vr_refresh_event_t event;
	int from, to; // FALL_BACK: the rates; others: from == the rate judged
	double fps; // the window's frames per second (WINDOW_* and FALL_BACK)
	double prev_fps; // FALL_BACK: the window before
	int misses;
} vr_refresh_step_t;

// wanted <= 0: never active (nothing is requested).
void vr_refresh_init(vr_refresh_t *r, int wanted);

// The runtime's offered rates (Hz, any order, junk skipped). Returns the rate to
// request first: the highest offered at or below `wanted`, else the lowest offered
// (above it); 0 when none are offered or nothing is wanted. Makes the machine
// active when more than one rate is offered, and the rate in effect the chosen one.
int vr_refresh_offer(vr_refresh_t *r, const float *rates, uint32_t count);

// The runtime reports the rate in effect (XrEventDataDisplayRefreshRateChangedFB).
// A different rate than believed adopts it and restarts the window.
void vr_refresh_adopt(vr_refresh_t *r, int rate);

// The runtime refused a request: stop trying.
void vr_refresh_give_up(vr_refresh_t *r);

// The next lower offered rate at or above the floor, 0 when there is none.
int vr_refresh_next_lower(const vr_refresh_t *r);

// One begun frame at its predicted display time. `worn`: the session is focused
// and shouldRender is true, and force_render is off. Fills `out` (always).
void vr_refresh_frame(vr_refresh_t *r, int64_t display_time_ns, bool worn, vr_refresh_step_t *out);

#endif
