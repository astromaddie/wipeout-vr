#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr_xr.h"
#include "vr_log.h"

// The game's own clamp on dt; vr_clock applies it to the display-time delta.
#define VR_MAX_DT 0.1


const char *vr_xr_state_name(XrSessionState state) {
	switch (state) {
		case XR_SESSION_STATE_IDLE: return "IDLE";
		case XR_SESSION_STATE_READY: return "READY";
		case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
		case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
		case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
		case XR_SESSION_STATE_STOPPING: return "STOPPING";
		case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
		case XR_SESSION_STATE_EXITING: return "EXITING";
		default: return "UNKNOWN";
	}
}

static bool check(const vr_xr_t *xr, XrResult r, const char *what) {
	if (XR_SUCCEEDED(r)) {
		return true;
	}
	char text[XR_MAX_RESULT_STRING_SIZE] = "?";
	if (xr->api.result_to_string && xr->instance != XR_NULL_HANDLE) {
		xr->api.result_to_string(xr->instance, r, text);
	}
	vr_logf("%s failed: %d (%s)", what, (int)r, text);
	return false;
}


// -----------------------------------------------------------------------------
// Pure helpers

uint32_t vr_xr_scaled_size(uint32_t recommended, uint32_t max, float scale) {
	if (!(scale > 0)) {
		scale = 1;
	}
	double scaled = floor((double)recommended * (double)scale + 0.5);
	if (scaled < 1) {
		scaled = 1;
	}
	if (max > 0 && scaled > (double)max) {
		scaled = (double)max;
	}
	return (uint32_t)scaled;
}

int vr_xr_choose_refresh_rate(const float *rates, uint32_t count, int wanted) {
	int best = 0;
	float best_distance = 0;
	for (uint32_t i = 0; i < count; i++) {
		int rate = (int)(rates[i] + 0.5f);
		float distance = fabsf(rates[i] - (float)wanted);
		if (rate > 0 && (best == 0 || distance < best_distance || (distance == best_distance && rate < best))) {
			best = rate;
			best_distance = distance;
		}
	}
	return best;
}

int64_t vr_xr_choose_color_format(const int64_t *formats, uint32_t count, bool *srgb) {
	static const int64_t preferred[] = { VR_GL_SRGB8_ALPHA8, VR_GL_SRGB8, VR_GL_RGBA8 };
	for (size_t p = 0; p < sizeof(preferred) / sizeof(preferred[0]); p++) {
		for (uint32_t i = 0; i < count; i++) {
			if (formats[i] == preferred[p]) {
				*srgb = (preferred[p] != VR_GL_RGBA8);
				return preferred[p];
			}
		}
	}
	*srgb = false;
	return 0;
}


// -----------------------------------------------------------------------------
// Instance

static bool load_function(vr_xr_t *xr, const char *name, void *out) {
	PFN_xrVoidFunction fn = NULL;
	XrResult r = xr->api.get_instance_proc_addr(xr->instance, name, &fn);
	if (XR_FAILED(r) || !fn) {
		return false;
	}
	memcpy(out, &fn, sizeof(fn));
	return true;
}

static bool extension_offered(const XrExtensionProperties *props, uint32_t count, const char *name) {
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(props[i].extensionName, name) == 0) {
			return true;
		}
	}
	return false;
}

bool vr_xr_instance_create(vr_xr_t *xr, const vr_xr_api_t *api, const vr_xr_config_t *config) {
	memset(xr, 0, sizeof(*xr));
	xr->api = *api;
	xr->config = *config;
	xr->state = XR_SESSION_STATE_UNKNOWN;
	xr->blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

	uint32_t ext_count = 0;
	if (!check(xr, xr->api.enumerate_instance_extension_properties(NULL, 0, &ext_count, NULL), "xrEnumerateInstanceExtensionProperties")) {
		return false;
	}
	XrExtensionProperties *props = calloc(ext_count + 1, sizeof(XrExtensionProperties));
	if (!props) {
		return false;
	}
	for (uint32_t i = 0; i < ext_count; i++) {
		props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	}
	if (!check(xr, xr->api.enumerate_instance_extension_properties(NULL, ext_count, &ext_count, props), "xrEnumerateInstanceExtensionProperties")) {
		free(props);
		return false;
	}
	bool has_gl = extension_offered(props, ext_count, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	bool has_valve = extension_offered(props, ext_count, XR_VALVE_FRAME_CONTROLLER_EXTENSION_NAME);
	bool has_refresh = extension_offered(props, ext_count, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
	free(props);

	if (!has_gl) {
		vr_logf("the runtime does not offer %s", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
		return false;
	}

	const char *names[3];
	uint32_t name_count = 0;
	names[name_count++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	if (has_valve) {
		names[name_count++] = XR_VALVE_FRAME_CONTROLLER_EXTENSION_NAME;
	}
	if (has_refresh) {
		names[name_count++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
	}

	XrInstanceCreateInfo ici = { .type = XR_TYPE_INSTANCE_CREATE_INFO };
	snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "%s", config->app_name);
	ici.applicationInfo.applicationVersion = 1;
	snprintf(ici.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "%s", "WipEoutVR");
	ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	ici.enabledExtensionCount = name_count;
	ici.enabledExtensionNames = names;
	if (!check(xr, xr->api.create_instance(&ici, &xr->instance), "xrCreateInstance")) {
		xr->instance = XR_NULL_HANDLE;
		return false;
	}

	xr->ext_frame_controller = has_valve;
	xr->ext_refresh_rate = has_refresh;

	bool ok = load_function(xr, "xrGetOpenGLGraphicsRequirementsKHR", &xr->api.get_opengl_graphics_requirements);
	if (!ok) {
		vr_logf("xrGetOpenGLGraphicsRequirementsKHR is not available");
	}
	if (ok && has_refresh) {
		if (!load_function(xr, "xrEnumerateDisplayRefreshRatesFB", &xr->api.enumerate_display_refresh_rates) ||
			!load_function(xr, "xrRequestDisplayRefreshRateFB", &xr->api.request_display_refresh_rate)) {
			xr->ext_refresh_rate = false;
		}
	}

	XrInstanceProperties ip = { .type = XR_TYPE_INSTANCE_PROPERTIES };
	if (ok && check(xr, xr->api.get_instance_properties(xr->instance, &ip), "xrGetInstanceProperties")) {
		snprintf(xr->runtime_name, sizeof(xr->runtime_name), "%s", ip.runtimeName);
		xr->runtime_version_major = XR_VERSION_MAJOR(ip.runtimeVersion);
		xr->runtime_version_minor = XR_VERSION_MINOR(ip.runtimeVersion);
		xr->runtime_version_patch = XR_VERSION_PATCH(ip.runtimeVersion);
	}
	else {
		ok = false;
	}

	XrSystemGetInfo sgi = { .type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY };
	ok = ok && check(xr, xr->api.get_system(xr->instance, &sgi, &xr->system_id), "xrGetSystem");

	XrSystemProperties sp = { .type = XR_TYPE_SYSTEM_PROPERTIES };
	ok = ok && check(xr, xr->api.get_system_properties(xr->instance, xr->system_id, &sp), "xrGetSystemProperties");
	if (ok) {
		snprintf(xr->system_name, sizeof(xr->system_name), "%s", sp.systemName);
	}

	XrViewConfigurationView vcv[2] = {
		{ .type = XR_TYPE_VIEW_CONFIGURATION_VIEW },
		{ .type = XR_TYPE_VIEW_CONFIGURATION_VIEW }
	};
	uint32_t view_count = 0;
	ok = ok && check(xr, xr->api.enumerate_view_configuration_views(
		xr->instance, xr->system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count, vcv
	), "xrEnumerateViewConfigurationViews");
	if (ok && view_count != 2) {
		vr_logf("the runtime reports %u views, expected 2", view_count);
		ok = false;
	}
	for (int e = 0; ok && e < 2; e++) {
		xr->eye_recommended[e][0] = vcv[e].recommendedImageRectWidth;
		xr->eye_recommended[e][1] = vcv[e].recommendedImageRectHeight;
		xr->eye_max[e][0] = vcv[e].maxImageRectWidth;
		xr->eye_max[e][1] = vcv[e].maxImageRectHeight;
	}

	XrEnvironmentBlendMode blend_modes[8];
	uint32_t blend_count = 0;
	ok = ok && check(xr, xr->api.enumerate_environment_blend_modes(
		xr->instance, xr->system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &blend_count, blend_modes
	), "xrEnumerateEnvironmentBlendModes");
	if (ok) {
		bool opaque = false;
		for (uint32_t i = 0; i < blend_count && i < 8; i++) {
			opaque |= (blend_modes[i] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
		}
		if (!opaque && blend_count > 0) {
			vr_logf("OPAQUE blend is not offered; using mode %d", (int)blend_modes[0]);
			xr->blend_mode = blend_modes[0];
		}
	}

	// Required before xrCreateSession for XR_KHR_opengl_enable
	XrGraphicsRequirementsOpenGLKHR reqs = { .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	ok = ok && check(xr, xr->api.get_opengl_graphics_requirements(xr->instance, xr->system_id, &reqs), "xrGetOpenGLGraphicsRequirementsKHR");
	xr->gl_min_version = reqs.minApiVersionSupported;
	xr->gl_max_version = reqs.maxApiVersionSupported;

	if (!ok) {
		xr->api.destroy_instance(xr->instance);
		xr->instance = XR_NULL_HANDLE;
		return false;
	}

	vr_logf("runtime %s %u.%u.%u, system %s", xr->runtime_name, xr->runtime_version_major, xr->runtime_version_minor, xr->runtime_version_patch, xr->system_name);
	vr_logf("extensions: opengl_enable, frame_controller_interaction=%s, display_refresh_rate=%s",
		xr->ext_frame_controller ? "yes" : "no", xr->ext_refresh_rate ? "yes" : "no");
	vr_logf("gl requirements: %u.%u to %u.%u",
		XR_VERSION_MAJOR(xr->gl_min_version), XR_VERSION_MINOR(xr->gl_min_version),
		XR_VERSION_MAJOR(xr->gl_max_version), XR_VERSION_MINOR(xr->gl_max_version));
	return true;
}


// -----------------------------------------------------------------------------
// Swapchains

bool vr_xr_swapchain_create(vr_xr_t *xr, vr_swapchain_t *sc, uint32_t width, uint32_t height) {
	memset(sc, 0, sizeof(*sc));

	XrSwapchainCreateInfo ci = {
		.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
		.format = xr->color_format,
		.sampleCount = 1,
		.width = width,
		.height = height,
		.faceCount = 1,
		.arraySize = 1,
		.mipCount = 1
	};
	if (!check(xr, xr->api.create_swapchain(xr->session, &ci, &sc->handle), "xrCreateSwapchain")) {
		sc->handle = XR_NULL_HANDLE;
		return false;
	}

	uint32_t count = 0;
	bool ok = check(xr, xr->api.enumerate_swapchain_images(sc->handle, 0, &count, NULL), "xrEnumerateSwapchainImages");
	if (ok && (count == 0 || count > VR_SWAPCHAIN_MAX_IMAGES)) {
		vr_logf("swapchain has %u images; at most %d are supported", count, VR_SWAPCHAIN_MAX_IMAGES);
		ok = false;
	}
	if (ok) {
		for (uint32_t i = 0; i < count; i++) {
			sc->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
			sc->images[i].next = NULL;
		}
		ok = check(xr, xr->api.enumerate_swapchain_images(
			sc->handle, count, &count, (XrSwapchainImageBaseHeader *)sc->images
		), "xrEnumerateSwapchainImages");
	}
	if (!ok) {
		xr->api.destroy_swapchain(sc->handle);
		memset(sc, 0, sizeof(*sc));
		return false;
	}

	sc->width = width;
	sc->height = height;
	sc->image_count = count;
	return true;
}

void vr_xr_swapchain_destroy(vr_xr_t *xr, vr_swapchain_t *sc) {
	if (sc->handle != XR_NULL_HANDLE) {
		xr->api.destroy_swapchain(sc->handle);
	}
	memset(sc, 0, sizeof(*sc));
}

bool vr_xr_swapchain_acquire(vr_xr_t *xr, vr_swapchain_t *sc, uint32_t *index) {
	XrSwapchainImageAcquireInfo ai = { .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	if (!check(xr, xr->api.acquire_swapchain_image(sc->handle, &ai, index), "xrAcquireSwapchainImage")) {
		return false;
	}
	sc->acquired = true;
	sc->acquired_index = *index;

	XrSwapchainImageWaitInfo wi = { .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION };
	if (!check(xr, xr->api.wait_swapchain_image(sc->handle, &wi), "xrWaitSwapchainImage")) {
		vr_xr_swapchain_release(xr, sc);
		return false;
	}
	return true;
}

bool vr_xr_swapchain_release(vr_xr_t *xr, vr_swapchain_t *sc) {
	if (!sc->acquired) {
		return false;
	}
	sc->acquired = false;
	XrSwapchainImageReleaseInfo ri = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	return check(xr, xr->api.release_swapchain_image(sc->handle, &ri), "xrReleaseSwapchainImage");
}


// -----------------------------------------------------------------------------
// Session

bool vr_xr_session_create(vr_xr_t *xr, const void *graphics_binding) {
	XrSessionCreateInfo sci = {
		.type = XR_TYPE_SESSION_CREATE_INFO,
		.next = graphics_binding,
		.systemId = xr->system_id
	};
	if (!check(xr, xr->api.create_session(xr->instance, &sci, &xr->session), "xrCreateSession")) {
		xr->session = XR_NULL_HANDLE;
		return false;
	}

	XrReferenceSpaceCreateInfo rsci = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
		.poseInReferenceSpace = { .orientation = { .w = 1.0f } }
	};
	if (!check(xr, xr->api.create_reference_space(xr->session, &rsci, &xr->local_space), "xrCreateReferenceSpace(LOCAL)")) {
		return false;
	}
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (!check(xr, xr->api.create_reference_space(xr->session, &rsci, &xr->view_space), "xrCreateReferenceSpace(VIEW)")) {
		return false;
	}

	// The game's output is gamma-encoded already, so the images are sRGB and
	// copies into them must not convert (GL_FRAMEBUFFER_SRGB off).
	uint32_t format_count = 0;
	int64_t formats[64];
	if (!check(xr, xr->api.enumerate_swapchain_formats(xr->session, 64, &format_count, formats), "xrEnumerateSwapchainFormats")) {
		return false;
	}
	if (format_count > 64) {
		format_count = 64;
	}
	xr->color_format = vr_xr_choose_color_format(formats, format_count, &xr->color_srgb);
	if (xr->color_format == 0) {
		vr_logf("no sRGB or RGBA8 swapchain format is offered");
		return false;
	}
	if (!xr->color_srgb) {
		vr_logf("WARNING: no sRGB swapchain format; using RGBA8, so colours will look dark");
	}

	for (int e = 0; e < 2; e++) {
		uint32_t w = vr_xr_scaled_size(xr->eye_recommended[e][0], xr->eye_max[e][0], xr->config.resolution_scale);
		uint32_t h = vr_xr_scaled_size(xr->eye_recommended[e][1], xr->eye_max[e][1], xr->config.resolution_scale);
		if (!vr_xr_swapchain_create(xr, &xr->eye[e], w, h)) {
			return false;
		}
	}
	return true;
}

void vr_xr_destroy(vr_xr_t *xr) {
	for (int e = 0; e < 2; e++) {
		vr_xr_swapchain_destroy(xr, &xr->eye[e]);
	}
	if (xr->session != XR_NULL_HANDLE && xr->session_running) {
		xr->api.end_session(xr->session);
		xr->session_running = false;
	}
	if (xr->local_space != XR_NULL_HANDLE) {
		xr->api.destroy_space(xr->local_space);
		xr->local_space = XR_NULL_HANDLE;
	}
	if (xr->view_space != XR_NULL_HANDLE) {
		xr->api.destroy_space(xr->view_space);
		xr->view_space = XR_NULL_HANDLE;
	}
	if (xr->session != XR_NULL_HANDLE) {
		xr->api.destroy_session(xr->session);
		xr->session = XR_NULL_HANDLE;
	}
	if (xr->instance != XR_NULL_HANDLE) {
		xr->api.destroy_instance(xr->instance);
		xr->instance = XR_NULL_HANDLE;
	}
	xr->frame_open = false;
}


// -----------------------------------------------------------------------------
// Session state machine

static void request_refresh_rate(vr_xr_t *xr) {
	if (!xr->ext_refresh_rate || xr->config.refresh_rate <= 0) {
		return;
	}

	float rates[VR_MAX_REFRESH_RATES];
	uint32_t count = 0;
	if (!check(xr, xr->api.enumerate_display_refresh_rates(xr->session, VR_MAX_REFRESH_RATES, &count, rates), "xrEnumerateDisplayRefreshRatesFB")) {
		return;
	}
	if (count > VR_MAX_REFRESH_RATES) {
		count = VR_MAX_REFRESH_RATES;
	}

	int chosen = vr_xr_choose_refresh_rate(rates, count, xr->config.refresh_rate);
	if (chosen == 0) {
		vr_logf("the runtime offers no refresh rates");
		return;
	}
	if (check(xr, xr->api.request_display_refresh_rate(xr->session, (float)chosen), "xrRequestDisplayRefreshRateFB")) {
		xr->refresh_rate_chosen = chosen;
		vr_logf("refresh rate: asked for %d Hz, runtime offers %u rates, requested %d Hz", xr->config.refresh_rate, count, chosen);
	}
}

static void on_session_state(vr_xr_t *xr, XrSessionState state) {
	bool was_focused = xr->focused;
	xr->state = state;
	xr->focused = (state == XR_SESSION_STATE_FOCUSED);
	if (xr->focused && !was_focused) {
		xr->focus_gains++;
	}
	vr_logf("session state %s", vr_xr_state_name(state));

	switch (state) {
		case XR_SESSION_STATE_READY:
			if (!xr->session_running) {
				XrSessionBeginInfo bi = {
					.type = XR_TYPE_SESSION_BEGIN_INFO,
					.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
				};
				if (check(xr, xr->api.begin_session(xr->session, &bi), "xrBeginSession")) {
					xr->session_running = true;
					vr_clock_reset(&xr->clock);
					request_refresh_rate(xr);
				}
			}
			break;

		case XR_SESSION_STATE_STOPPING:
			if (xr->session_running) {
				check(xr, xr->api.end_session(xr->session), "xrEndSession");
				xr->session_running = false;
			}
			xr->frame_open = false;
			vr_clock_reset(&xr->clock);
			break;

		case XR_SESSION_STATE_EXITING:
		case XR_SESSION_STATE_LOSS_PENDING:
			xr->exit_requested = true;
			break;

		default:
			// SYNCHRONIZED, VISIBLE and FOCUSED keep the loop running. A headset in
			// standby stays in SYNCHRONIZED with shouldRender false.
			break;
	}
}

void vr_xr_poll_events(vr_xr_t *xr) {
	for (;;) {
		XrEventDataBuffer ev = { .type = XR_TYPE_EVENT_DATA_BUFFER };
		if (xr->api.poll_event(xr->instance, &ev) != XR_SUCCESS) {
			break; // XR_EVENT_UNAVAILABLE: the queue is empty
		}

		switch (ev.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
				on_session_state(xr, ((const XrEventDataSessionStateChanged *)&ev)->state);
				break;

			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				vr_logf("instance loss pending");
				xr->exit_requested = true;
				break;

			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
				// The runtime moves the LOCAL origin (a system recentre). The caller
				// re-anchors whatever it placed in LOCAL space.
				xr->recentre_requests++;
				vr_logf("reference space change pending");
				break;

			default:
				break;
		}
	}
}

void vr_xr_request_exit(vr_xr_t *xr) {
	if (xr->session != XR_NULL_HANDLE && xr->session_running) {
		check(xr, xr->api.request_exit_session(xr->session), "xrRequestExitSession");
	}
}


// -----------------------------------------------------------------------------
// Frame loop

bool vr_xr_sim_active(const vr_xr_t *xr) {
	return xr->session_running && xr->frame.should_render && xr->focused;
}

static void view_from_xr(vr_view_t *out, const XrView *in) {
	out->pose.orientation[0] = in->pose.orientation.x;
	out->pose.orientation[1] = in->pose.orientation.y;
	out->pose.orientation[2] = in->pose.orientation.z;
	out->pose.orientation[3] = in->pose.orientation.w;
	out->pose.position[0] = in->pose.position.x;
	out->pose.position[1] = in->pose.position.y;
	out->pose.position[2] = in->pose.position.z;
	out->fov[0] = in->fov.angleLeft;
	out->fov[1] = in->fov.angleRight;
	out->fov[2] = in->fov.angleUp;
	out->fov[3] = in->fov.angleDown;
}

// A head at 1.6 m looking down -Z, for force_render in standby.
static void synthetic_views(vr_frame_t *frame) {
	for (int e = 0; e < 2; e++) {
		vr_view_t *v = &frame->views[e];
		memset(v, 0, sizeof(*v));
		v->pose.orientation[3] = 1;
		v->pose.position[0] = e == 0 ? -0.032f : 0.032f;
		v->pose.position[1] = 1.6f;
		v->fov[0] = -0.8f;
		v->fov[1] = 0.8f;
		v->fov[2] = 0.8f;
		v->fov[3] = -0.8f;
	}
	frame->views_valid = true;
}

bool vr_xr_frame_begin(vr_xr_t *xr) {
	xr->frame_open = false;
	if (!xr->session_running) {
		return false;
	}

	XrFrameWaitInfo wi = { .type = XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState fs = { .type = XR_TYPE_FRAME_STATE };
	XrResult r = xr->api.wait_frame(xr->session, &wi, &fs);
	if (!check(xr, r, "xrWaitFrame")) {
		xr->exit_requested |= (r == XR_ERROR_SESSION_LOST);
		return false;
	}

	XrFrameBeginInfo bi = { .type = XR_TYPE_FRAME_BEGIN_INFO };
	r = xr->api.begin_frame(xr->session, &bi);
	if (!check(xr, r, "xrBeginFrame")) {
		xr->exit_requested |= (r == XR_ERROR_SESSION_LOST);
		return false;
	}
	// XR_FRAME_DISCARDED: the previous frame was begun and never ended. Fine.

	memset(&xr->frame, 0, sizeof(xr->frame));
	xr->frame.predicted_display_time = fs.predictedDisplayTime;
	xr->frame.predicted_display_period = fs.predictedDisplayPeriod;
	xr->frame.should_render = fs.shouldRender != XR_FALSE;

	if (xr->frame.should_render) {
		XrViewLocateInfo li = {
			.type = XR_TYPE_VIEW_LOCATE_INFO,
			.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			.displayTime = fs.predictedDisplayTime,
			.space = xr->local_space
		};
		XrViewState vs = { .type = XR_TYPE_VIEW_STATE };
		XrView views[2] = { { .type = XR_TYPE_VIEW }, { .type = XR_TYPE_VIEW } };
		uint32_t located = 0;
		XrResult lr = xr->api.locate_views(xr->session, &li, &vs, 2, &located, views);
		if (check(xr, lr, "xrLocateViews") && located == 2 &&
			(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
			(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
		) {
			view_from_xr(&xr->frame.views[0], &views[0]);
			view_from_xr(&xr->frame.views[1], &views[1]);
			xr->frame.views_valid = true;
		}
	}
	else if (xr->config.force_render) {
		synthetic_views(&xr->frame);
	}

	xr->frame_dt = vr_clock_step(&xr->clock, fs.predictedDisplayTime, vr_xr_sim_active(xr), VR_MAX_DT);
	xr->frame_open = true;
	xr->frames_begun++;
	return true;
}

bool vr_xr_frame_end(vr_xr_t *xr, const XrCompositionLayerBaseHeader *const *layers, uint32_t layer_count) {
	if (!xr->frame_open) {
		return false;
	}
	xr->frame_open = false;

	if (!xr->frame.should_render && !xr->config.force_render) {
		layers = NULL;
		layer_count = 0;
	}

	XrFrameEndInfo ei = {
		.type = XR_TYPE_FRAME_END_INFO,
		.displayTime = xr->frame.predicted_display_time,
		.environmentBlendMode = xr->blend_mode,
		.layerCount = layer_count,
		.layers = layers
	};
	if (!check(xr, xr->api.end_frame(xr->session, &ei), "xrEndFrame")) {
		return false;
	}
	xr->frames_ended++;
	return true;
}

bool vr_xr_locate_head(vr_xr_t *xr, vr_pose_t *head) {
	XrSpaceLocation loc = { .type = XR_TYPE_SPACE_LOCATION };
	XrResult r = xr->api.locate_space(xr->view_space, xr->local_space, xr->frame.predicted_display_time, &loc);
	if (XR_FAILED(r) ||
		!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
		!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
	) {
		return false;
	}
	head->orientation[0] = loc.pose.orientation.x;
	head->orientation[1] = loc.pose.orientation.y;
	head->orientation[2] = loc.pose.orientation.z;
	head->orientation[3] = loc.pose.orientation.w;
	head->position[0] = loc.pose.position.x;
	head->position[1] = loc.pose.position.y;
	head->position[2] = loc.pose.position.z;
	return true;
}

void vr_xr_quad_layer(const vr_xr_t *xr, XrCompositionLayerQuad *layer, const vr_swapchain_t *sc, const vr_pose_t *pose, float width_m) {
	memset(layer, 0, sizeof(*layer));
	layer->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
	layer->space = xr->local_space;
	layer->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	layer->subImage.swapchain = sc->handle;
	layer->subImage.imageRect.extent.width = (int32_t)sc->width;
	layer->subImage.imageRect.extent.height = (int32_t)sc->height;
	layer->pose.orientation.x = pose->orientation[0];
	layer->pose.orientation.y = pose->orientation[1];
	layer->pose.orientation.z = pose->orientation[2];
	layer->pose.orientation.w = pose->orientation[3];
	layer->pose.position.x = pose->position[0];
	layer->pose.position.y = pose->position[1];
	layer->pose.position.z = pose->position[2];
	layer->size.width = width_m;
	layer->size.height = sc->width > 0 ? width_m * (float)sc->height / (float)sc->width : width_m;
}

void vr_xr_projection_layer(const vr_xr_t *xr, XrCompositionLayerProjection *layer, XrCompositionLayerProjectionView views[2]) {
	for (int e = 0; e < 2; e++) {
		const vr_view_t *v = &xr->frame.views[e];
		memset(&views[e], 0, sizeof(views[e]));
		views[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		views[e].pose.orientation.x = v->pose.orientation[0];
		views[e].pose.orientation.y = v->pose.orientation[1];
		views[e].pose.orientation.z = v->pose.orientation[2];
		views[e].pose.orientation.w = v->pose.orientation[3];
		views[e].pose.position.x = v->pose.position[0];
		views[e].pose.position.y = v->pose.position[1];
		views[e].pose.position.z = v->pose.position[2];
		views[e].fov.angleLeft = v->fov[0];
		views[e].fov.angleRight = v->fov[1];
		views[e].fov.angleUp = v->fov[2];
		views[e].fov.angleDown = v->fov[3];
		views[e].subImage.swapchain = xr->eye[e].handle;
		views[e].subImage.imageRect.extent.width = (int32_t)xr->eye[e].width;
		views[e].subImage.imageRect.extent.height = (int32_t)xr->eye[e].height;
	}
	memset(layer, 0, sizeof(*layer));
	layer->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	layer->space = xr->local_space;
	layer->viewCount = 2;
	layer->views = views;
}
