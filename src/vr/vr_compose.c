#include <string.h>

#include "vr_compose.h"

static void override_view(XrCompositionLayerProjectionView *pv, const vr_view_t *v) {
	pv->pose.orientation.x = v->pose.orientation[0];
	pv->pose.orientation.y = v->pose.orientation[1];
	pv->pose.orientation.z = v->pose.orientation[2];
	pv->pose.orientation.w = v->pose.orientation[3];
	pv->pose.position.x = v->pose.position[0];
	pv->pose.position.y = v->pose.position[1];
	pv->pose.position.z = v->pose.position[2];
	pv->fov.angleLeft = v->fov[0];
	pv->fov.angleRight = v->fov[1];
	pv->fov.angleUp = v->fov[2];
	pv->fov.angleDown = v->fov[3];
}

uint32_t vr_compose_layers(vr_compose_t *c, const vr_xr_t *xr, const vr_layers_t *l,
	const vr_swapchain_t *hud, const vr_swapchain_t *panel, const vr_swapchain_t *fade
) {
	c->count = 0;
	if (!l) {
		return 0;
	}

	if (l->projection) {
		vr_xr_projection_layer(xr, &c->projection, c->projection_views);
		for (int e = 0; e < 2; e++) {
			override_view(&c->projection_views[e], &l->views[e]);
		}
		c->list[c->count++] = (const XrCompositionLayerBaseHeader *)&c->projection;
	}

	if (l->hud && hud && hud->handle != XR_NULL_HANDLE) {
		vr_xr_quad_layer(xr, &c->hud, hud, &l->hud_pose, l->hud_width);
		// The HUD's alpha is derived (renders over black and white), and straight
		c->hud.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
		c->list[c->count++] = (const XrCompositionLayerBaseHeader *)&c->hud;
	}

	if (l->panel && panel && panel->handle != XR_NULL_HANDLE) {
		vr_xr_quad_layer(xr, &c->panel, panel, &l->panel_pose, l->panel_width);
		c->list[c->count++] = (const XrCompositionLayerBaseHeader *)&c->panel;
	}

	if (l->fade_alpha > VR_FADE_MIN_ALPHA && fade && fade->handle != XR_NULL_HANDLE) {
		vr_pose_t pose = { .orientation = {0, 0, 0, 1}, .position = {0, 0, -VR_FADE_DISTANCE} };
		vr_xr_quad_layer(xr, &c->fade, fade, &pose, VR_FADE_SIZE);
		c->fade.space = xr->view_space;
		c->fade.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		c->list[c->count++] = (const XrCompositionLayerBaseHeader *)&c->fade;
	}
	return c->count;
}
