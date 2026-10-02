// What a frame submits to the compositor: the layer structs built from the
// renderer's vr_layers_t. No runtime and no GL: vr_compose_layers() only fills
// structs.

#include <stdint.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_compose.h"

#define HANDLE(n) ((void *)(uintptr_t)(n))

static vr_xr_t xr;
static vr_swapchain_t hud, panel, fade;

static void setup(void) {
	memset(&xr, 0, sizeof(xr));
	xr.local_space = HANDLE(0x31);
	xr.view_space = HANDLE(0x32);
	for (int e = 0; e < 2; e++) {
		xr.eye[e].handle = HANDLE(0x41 + e);
		xr.eye[e].width = 1728;
		xr.eye[e].height = 1728;
		// what the runtime located: not what the renderer used
		xr.frame.views[e].pose.orientation[3] = 1;
		xr.frame.views[e].pose.position[0] = e ? 0.5f : -0.5f;
		xr.frame.views[e].fov[0] = -0.1f; xr.frame.views[e].fov[1] = 0.1f;
		xr.frame.views[e].fov[2] = 0.1f; xr.frame.views[e].fov[3] = -0.1f;
	}
	memset(&hud, 0, sizeof(hud));
	hud.handle = HANDLE(0x51);
	hud.width = 1920;
	hud.height = 1080;
	memset(&panel, 0, sizeof(panel));
	panel.handle = HANDLE(0x52);
	panel.width = 1920;
	panel.height = 1080;
	memset(&fade, 0, sizeof(fade));
	fade.handle = HANDLE(0x53);
	fade.width = 4;
	fade.height = 4;
}

static vr_layers_t stereo_layers(void) {
	vr_layers_t l;
	memset(&l, 0, sizeof(l));
	l.projection = true;
	for (int e = 0; e < 2; e++) {
		l.views[e].pose.orientation[1] = 0.6f; // a turned head: diag_yaw
		l.views[e].pose.orientation[3] = 0.8f;
		l.views[e].pose.position[0] = e ? 0.032f : -0.032f;
		l.views[e].pose.position[1] = 1.6f;
		l.views[e].fov[0] = -0.9f; l.views[e].fov[1] = 0.7f; l.views[e].fov[2] = 0.8f; l.views[e].fov[3] = -0.8f;
	}
	l.hud = true;
	l.hud_pose.orientation[3] = 1;
	l.hud_pose.position[1] = 1.6f;
	l.hud_pose.position[2] = -2.0f;
	l.hud_width = 1.456f;
	l.hud_height = 0.819f;
	return l;
}

static void test_stereo_layers(void) {
	setup();
	vr_layers_t l = stereo_layers();
	vr_compose_t c;
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, NULL), 2);

	// the projection layer first, rendered with the renderer's poses and fovs
	const XrCompositionLayerProjection *p = (const XrCompositionLayerProjection *)c.list[0];
	CHECK_INT(p->type, XR_TYPE_COMPOSITION_LAYER_PROJECTION);
	CHECK(p->space == xr.local_space);
	CHECK_INT(p->viewCount, 2);
	for (int e = 0; e < 2; e++) {
		const XrCompositionLayerProjectionView *v = &p->views[e];
		CHECK_NEAR(v->pose.orientation.y, 0.6, 1e-6);
		CHECK_NEAR(v->pose.orientation.w, 0.8, 1e-6);
		CHECK_NEAR(v->pose.position.x, e ? 0.032 : -0.032, 1e-6);
		CHECK_NEAR(v->pose.position.y, 1.6, 1e-6);
		CHECK_NEAR(v->fov.angleLeft, -0.9, 1e-6);
		CHECK_NEAR(v->fov.angleRight, 0.7, 1e-6);
		CHECK(v->subImage.swapchain == xr.eye[e].handle);
		CHECK_INT(v->subImage.imageRect.extent.width, 1728);
	}

	// then the HUD quad over it, 2 m ahead, blending its straight alpha
	const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)c.list[1];
	CHECK_INT(q->type, XR_TYPE_COMPOSITION_LAYER_QUAD);
	CHECK(q->space == xr.local_space);
	CHECK(q->subImage.swapchain == hud.handle);
	CHECK(q->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
	CHECK(q->layerFlags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT);
	CHECK_NEAR(q->pose.position.z, -2.0, 1e-6);
	CHECK_NEAR(q->size.width, 1.456, 1e-6);
	CHECK_NEAR(q->size.height, 1.456 * 1080.0 / 1920.0, 1e-4); // the texture's aspect
	CHECK_INT(q->eyeVisibility, XR_EYE_VISIBILITY_BOTH);
}

static void test_mono_layer(void) {
	setup();
	vr_layers_t l;
	memset(&l, 0, sizeof(l));
	l.panel = true;
	l.panel_pose.orientation[3] = 1;
	l.panel_pose.position[2] = -2.0f;
	l.panel_width = 2.4f;
	vr_compose_t c;
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, NULL), 1);
	const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)c.list[0];
	CHECK(q->space == xr.local_space);
	CHECK(q->subImage.swapchain == panel.handle);
	CHECK_INT(q->layerFlags, 0); // opaque
	CHECK_NEAR(q->size.width, 2.4, 1e-6);
	CHECK_NEAR(q->size.height, 2.4 * 1080.0 / 1920.0, 1e-4);
}

static void test_fade_goes_on_top_in_view_space(void) {
	setup();
	vr_layers_t l = stereo_layers();
	l.fade_alpha = 0.6f;
	vr_compose_t c;
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, &fade), 3);
	const XrCompositionLayerQuad *q = (const XrCompositionLayerQuad *)c.list[2];
	CHECK(q->subImage.swapchain == fade.handle);
	CHECK(q->space == xr.view_space); // glued to the head
	CHECK(q->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
	CHECK_NEAR(q->pose.position.z, -VR_FADE_DISTANCE, 1e-6);
	CHECK_NEAR(q->size.width, VR_FADE_SIZE, 1e-6);

	// no fade: not submitted, even when the swapchain is there; and none without the swapchain
	l.fade_alpha = 0.0f;
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, &fade), 2);
	l.fade_alpha = 1.0f;
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, NULL), 2);

	// the fade covers a mono panel too
	vr_layers_t m;
	memset(&m, 0, sizeof(m));
	m.panel = true;
	m.panel_width = 2.4f;
	m.fade_alpha = 1.0f;
	CHECK_INT(vr_compose_layers(&c, &xr, &m, &hud, &panel, &fade), 2);
	CHECK(((const XrCompositionLayerQuad *)c.list[1])->subImage.swapchain == fade.handle);
}

static void test_nothing_to_submit(void) {
	setup();
	vr_compose_t c;
	vr_layers_t l;
	memset(&l, 0, sizeof(l)); // standby: the renderer submits an empty frame
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, &fade), 0);
	CHECK_INT(vr_compose_layers(&c, &xr, NULL, &hud, &panel, &fade), 0);

	// a HUD with no swapchain behind it is dropped, not submitted empty
	l = stereo_layers();
	hud.handle = XR_NULL_HANDLE;
	CHECK_INT(vr_compose_layers(&c, &xr, &l, &hud, &panel, NULL), 1);
}

int main(void) {
	RUN(test_stereo_layers);
	RUN(test_mono_layer);
	RUN(test_fade_goes_on_top_in_view_space);
	RUN(test_nothing_to_submit);
	return SFVR_TEST_RESULT("vr_compose");
}
