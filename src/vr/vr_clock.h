#ifndef VR_CLOCK_H
#define VR_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

// Turns the change in the runtime's predictedDisplayTime into the game's dt.

typedef struct {
	int64_t last_time; // nanoseconds
	bool has_last;
} vr_clock_t;

void vr_clock_reset(vr_clock_t *c);

// dt in seconds for a frame with this predictedDisplayTime. 0 for the first
// frame after a reset, when the time did not advance, or when `sim_active` is
// false (standby or no focus). The time base is always advanced, so the first
// active frame after a freeze gets a normal dt. Never above `max_dt`.
double vr_clock_step(vr_clock_t *c, int64_t predicted_display_time, bool sim_active, double max_dt);

#endif
