#ifndef VR_RENDER_H
#define VR_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "vr_cmd.h"
#include "vr_stage.h"
#include "vr_types.h"

// The stereo renderer's public face (docs/VR_ARCHITECTURE.md sections 3 and 4).
// The implementation is vr_render_gl.inc, compiled as part of render_gl.c
// because it needs the renderer's own GL state.
//
// Attached, the renderer records the game's draw calls (render_frame_prepare()
// to render_frame_end()) and replays them: a stereo frame per eye into the eye
// swapchain images plus once into the HUD image, a mono frame once into the
// panel image. It asks a provider for the frame's head pose and for images to
// draw into, and hands back what to submit. vr.c is the provider for the real
// runtime; the host acceptance harness (tests/vr/vr_render_harness.c) is a
// fake one.

typedef enum {
	VR_SURFACE_EYE0,
	VR_SURFACE_EYE1,
	VR_SURFACE_HUD, // ui_width x ui_height, an alpha-capable format
	VR_SURFACE_PANEL, // ui_width x ui_height
	VR_SURFACE_COUNT
} vr_surface_t;

typedef struct {
	uint32_t image; // a GL texture name
	uint32_t width, height;
} vr_target_t;

typedef struct {
	void *user;
	// The frame's views; *renders is false in standby (nothing is replayed then).
	const vr_frame_t *(*frame)(void *user, bool *renders);
	// Acquire an image to draw into; false when it cannot be had. Release every
	// acquired image.
	bool (*acquire)(void *user, vr_surface_t surface, vr_target_t *out);
	void (*release)(void *user, vr_surface_t surface);
	// What the frame shows, after the replay
	void (*submit)(void *user, const vr_layers_t *layers);
} vr_provider_t;

typedef struct {
	vr_provider_t provider;
	vr_render_settings_t settings;
	const vr_settings_t *live_settings; // when set, the cockpit keys are re-read from it every frame
	const char *dump_dir; // where dump_frame writes; NULL for none
} vr_render_config_t;

// Call after the GL context exists and before render_init(): the renderer
// then takes its 2D layout size from settings.ui_width/ui_height, ignores the
// window size, the low-resolution modes and the post effects, and records.
void vr_render_attach(const vr_render_config_t *config);
// Writes pending dumps and goes back to the flat path.
void vr_render_detach(void);
bool vr_render_active(void);

// A recentre was asked for: applied at the next stereo frame, and the panel re-placed.
void vr_render_request_recentre(void);

// Dump the frame_number'th replayed frame (counting from 1) into `dir`
// (vr-eye0.bmp, vr-eye1.bmp, vr-hud.bmp for a stereo frame; vr-panel.bmp for a
// mono one; vr-state.txt). The pixels are read back one frame late.
void vr_render_dump_request(int frame_number, const char *dir);
// Blocks until every pending dump is written. For shutdown and tests.
void vr_render_dump_flush(void);

typedef struct {
	float record_ms; // recording the game's calls (only measured when settings.timing is set)
	float replay_ms; // both eyes
	float hud_ms;
	float eye_ms[2];
} vr_render_timing_t;
const vr_render_timing_t *vr_render_timing(void);

typedef struct {
	vr_mode_t mode;
	uint32_t frames_replayed;
	uint32_t commands, triangles, sprites, draw_calls;
	uint32_t face_passes; // instrument faces drawn into their textures this frame
	bool recentred;
	float fade_alpha;
} vr_render_frame_info_t;
const vr_render_frame_info_t *vr_render_frame_info(void);

// The GL texture of an instrument face (0 before its first draw), for the
// harness: it reads it back; nothing in a normal frame does.
uint32_t vr_render_face_texture(int face);

// A camera set the last frame recorded (a render_set_view each; stereo frames
// have the eyes in it), for the harness. False past the last one.
bool vr_render_view(int index, vr_camset_t *out);

#endif
