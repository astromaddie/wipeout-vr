#ifndef VR_COMPOSE_H
#define VR_COMPOSE_H

#include "vr_stage.h"
#include "vr_xr.h"

// Turns what the renderer submitted for a frame (vr_layers_t) into the OpenXR
// composition layers for vr_xr_frame_end(). No runtime calls and no GL: it only
// fills structs, so tests check the result.
//
// Order, bottom to top: the projection layer (stereo), the HUD quad (stereo,
// with alpha), the panel quad (mono), the black fade quad. The eyes are
// submitted with the poses and fovs they were rendered with, which are the
// layers' views (diag_yaw can turn them), not the runtime's.

// The fade is a black quad in VIEW space: close enough to cover the lens
#define VR_FADE_DISTANCE 0.35f
#define VR_FADE_SIZE 2.6f
#define VR_FADE_MIN_ALPHA 0.002f

typedef struct {
	XrCompositionLayerProjection projection;
	XrCompositionLayerProjectionView projection_views[2];
	XrCompositionLayerQuad hud;
	XrCompositionLayerQuad panel;
	XrCompositionLayerQuad fade;
	const XrCompositionLayerBaseHeader *list[4];
	uint32_t count;
} vr_compose_t;

// `fade` is the swapchain whose image was just cleared to black at
// layers->fade_alpha; NULL when there is none. Returns the layer count (c->list).
uint32_t vr_compose_layers(vr_compose_t *c, const vr_xr_t *xr, const vr_layers_t *layers,
	const vr_swapchain_t *hud, const vr_swapchain_t *panel, const vr_swapchain_t *fade);

#endif
