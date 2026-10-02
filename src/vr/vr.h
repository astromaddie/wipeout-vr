#ifndef VR_H
#define VR_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_types.h"
#include "vr_settings.h"

// The VR host as the game's platform layer sees it (WIPEOUT_VR builds only).
//
// Per frame, from platform_sdl.c:
//   vr_frame_begin()          xrWaitFrame + xrBeginFrame + xrLocateViews
//   system_update()           dt from vr_frame_dt(); the sim is frozen when the
//                             frame is not rendered or the app has no focus
//   vr_present_flat_frame()   the game's normal frame into a world-locked panel,
//                             then vr_frame_end()
//
// The stereo renderer (next milestone) replaces the panel for race frames:
// vr_eye_acquire()/vr_eye_release() per eye, then vr_submit_projection().

// Reads vr.toml, creates the XR instance and session on the SDL window's
// current GL context. False (with the reason logged) when VR is unavailable;
// the game then runs flat as upstream does.
bool vr_init(void *sdl_window);
void vr_shutdown(void);

bool vr_enabled(void);
bool vr_exit_requested(void);
const vr_settings_t *vr_settings(void);

// vr.probe_seconds: left eye red, right eye blue for that long. Call after
// vr_init() and before any game code.
void vr_probe(void);

// False when no frame was begun: the session is not running (yet), so the
// caller should not run the game. When true, vr_frame_end() or
// vr_present_flat_frame() must follow.
bool vr_frame_begin(void);
void vr_frame_end(void);
const vr_frame_t *vr_frame(void);

// Seconds since the previous frame, from the change in predictedDisplayTime.
// 0 when the sim is frozen. The caller clamps it as it always did.
double vr_frame_dt(void);

// shouldRender is false (standby) or the app has no focus: dt is 0 and audio
// should be paused.
bool vr_sim_frozen(void);

// Copies the window's back buffer (src_width x src_height) into the panel
// swapchain, queues the panel quad and ends the frame.
void vr_present_flat_frame(uint32_t src_width, uint32_t src_height);

// Seams for the stereo renderer
bool vr_eye_acquire(int eye, uint32_t *gl_image, uint32_t *width, uint32_t *height);
void vr_eye_release(int eye);
void vr_submit_projection(void);

#endif
