// The XR host's lifecycle against a fake runtime: no loader, no headset, no
// window. The fake implements vr_xr_api_t and runs a script of events and
// frame states.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sfvr_test.h"
#include "vr_log.h"
#include "vr_xr.h"

#define HANDLE(n) ((void *)(uintptr_t)(n))
#define INSTANCE HANDLE(0x10)
#define SESSION HANDLE(0x20)
#define LOCAL_SPACE HANDLE(0x31)
#define VIEW_SPACE HANDLE(0x32)

#define FRAME_PERIOD_NS 13888889 // 72 Hz

// -----------------------------------------------------------------------------
// The fake runtime. The API has no user pointer, so its state is global.

#define QUEUE_MAX 32
#define MAX_SWAPCHAINS 8

static struct {
	// What the runtime offers
	bool offers_gl, offers_valve, offers_refresh;
	bool refresh_fns_missing;
	float refresh_rates[8];
	uint32_t refresh_rate_count;
	int64_t formats[8];
	uint32_t format_count;
	uint32_t recommended[2], max[2];
	XrEnvironmentBlendMode blend_modes[2];
	uint32_t blend_count;
	uint32_t gl_min_major, gl_min_minor;

	// Script
	XrEventDataBuffer queue[QUEUE_MAX];
	uint32_t queue_head, queue_tail;
	bool should_render;
	XrTime time;
	XrViewStateFlags view_flags;
	XrResult wait_frame_result;
	XrResult begin_frame_result;
	XrSpaceLocationFlags head_flags;
	XrPosef head_pose;
	bool stop_on_exit_request;

	// What the host asked for
	int create_instance_calls;
	char enabled_extensions[8][XR_MAX_EXTENSION_NAME_SIZE];
	uint32_t enabled_extension_count;
	int destroy_instance_calls;
	int create_session_calls;
	const void *session_binding;
	int destroy_session_calls;
	int space_creates[4]; // by reference space type
	int destroy_space_calls;
	int begin_session_calls;
	XrViewConfigurationType begin_session_view_config;
	int end_session_calls;
	int request_exit_calls;
	int wait_frame_calls;
	int begin_frame_calls;
	int end_frame_calls;
	int locate_views_calls;
	int locate_space_calls;
	uint32_t last_layer_count;
	XrStructureType last_layer_type;
	XrTime last_end_time;
	XrEnvironmentBlendMode last_blend;
	int request_refresh_calls;
	float requested_refresh;
	int enumerate_refresh_calls;
	int swapchain_creates;
	XrSwapchainCreateInfo swapchain_info[MAX_SWAPCHAINS];
	int swapchain_destroys;
	int acquires, waits, releases;
	char calls[64][32]; // names of the lifecycle calls, in order
	int call_count;
} fake;

static void fake_call(const char *name) {
	if (fake.call_count < 64) {
		snprintf(fake.calls[fake.call_count++], 32, "%s", name);
	}
}

static void fake_reset(void) {
	memset(&fake, 0, sizeof(fake));
	fake.offers_gl = true;
	fake.offers_valve = true;
	fake.offers_refresh = true;
	fake.refresh_rates[0] = 72; fake.refresh_rates[1] = 90; fake.refresh_rates[2] = 144;
	fake.refresh_rate_count = 3;
	fake.formats[0] = VR_GL_SRGB8; fake.formats[1] = VR_GL_SRGB8_ALPHA8;
	fake.format_count = 2;
	fake.recommended[0] = fake.recommended[1] = 1728;
	fake.max[0] = fake.max[1] = 8192;
	fake.blend_modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	fake.blend_modes[1] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
	fake.blend_count = 2;
	fake.gl_min_major = 4; fake.gl_min_minor = 3;
	fake.should_render = true;
	fake.time = 1000000000;
	fake.view_flags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
	fake.wait_frame_result = XR_SUCCESS;
	fake.begin_frame_result = XR_SUCCESS;
	fake.head_flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	fake.head_pose.orientation.w = 1;
	fake.head_pose.position.y = 1.6f;
	fake.stop_on_exit_request = true;
}

static void push_event(const void *event, size_t size) {
	XrEventDataBuffer *slot = &fake.queue[fake.queue_tail++ % QUEUE_MAX];
	memset(slot, 0, sizeof(*slot));
	memcpy(slot, event, size);
}

static void push_state(XrSessionState state) {
	XrEventDataSessionStateChanged ev = {
		.type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED,
		.session = SESSION,
		.state = state
	};
	push_event(&ev, sizeof(ev));
}

static void push_reference_space_change(void) {
	XrEventDataReferenceSpaceChangePending ev = {
		.type = XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING,
		.session = SESSION,
		.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL
	};
	push_event(&ev, sizeof(ev));
}

static void push_instance_loss(void) {
	XrEventDataInstanceLossPending ev = { .type = XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING };
	push_event(&ev, sizeof(ev));
}

static XrResult XRAPI_CALL fake_enumerate_extensions(const char *layer, uint32_t capacity, uint32_t *count, XrExtensionProperties *props) {
	(void)layer;
	const char *offered[4];
	uint32_t n = 0;
	if (fake.offers_gl) offered[n++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	if (fake.offers_valve) offered[n++] = XR_VALVE_FRAME_CONTROLLER_EXTENSION_NAME;
	if (fake.offers_refresh) offered[n++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
	offered[n++] = "XR_FAKE_something_else";
	*count = n;
	if (capacity == 0) {
		return XR_SUCCESS;
	}
	for (uint32_t i = 0; i < n && i < capacity; i++) {
		strcpy(props[i].extensionName, offered[i]);
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_create_instance(const XrInstanceCreateInfo *info, XrInstance *instance) {
	fake.create_instance_calls++;
	fake.enabled_extension_count = info->enabledExtensionCount;
	for (uint32_t i = 0; i < info->enabledExtensionCount && i < 8; i++) {
		strcpy(fake.enabled_extensions[i], info->enabledExtensionNames[i]);
	}
	*instance = INSTANCE;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_destroy_instance(XrInstance instance) {
	(void)instance;
	fake.destroy_instance_calls++;
	fake_call("destroy_instance");
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_instance_properties(XrInstance instance, XrInstanceProperties *p) {
	(void)instance;
	strcpy(p->runtimeName, "Fake Runtime");
	p->runtimeVersion = XR_MAKE_VERSION(2, 17, 10);
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_result_to_string(XrInstance instance, XrResult r, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
	(void)instance;
	snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_FAKE_%d", (int)r);
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_get_system(XrInstance instance, const XrSystemGetInfo *info, XrSystemId *id) {
	(void)instance;
	if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
		return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
	}
	*id = 7;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_system_properties(XrInstance instance, XrSystemId id, XrSystemProperties *p) {
	(void)instance; (void)id;
	strcpy(p->systemName, "Fake HMD");
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_view_configuration_views(XrInstance instance, XrSystemId id, XrViewConfigurationType type, uint32_t capacity, uint32_t *count, XrViewConfigurationView *views) {
	(void)instance; (void)id; (void)type;
	*count = 2;
	for (uint32_t i = 0; i < 2 && i < capacity; i++) {
		views[i].recommendedImageRectWidth = fake.recommended[0];
		views[i].recommendedImageRectHeight = fake.recommended[1];
		views[i].maxImageRectWidth = fake.max[0];
		views[i].maxImageRectHeight = fake.max[1];
		views[i].recommendedSwapchainSampleCount = 1;
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_blend_modes(XrInstance instance, XrSystemId id, XrViewConfigurationType type, uint32_t capacity, uint32_t *count, XrEnvironmentBlendMode *modes) {
	(void)instance; (void)id; (void)type;
	*count = fake.blend_count;
	for (uint32_t i = 0; i < fake.blend_count && i < capacity; i++) {
		modes[i] = fake.blend_modes[i];
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_gl_requirements(XrInstance instance, XrSystemId id, XrGraphicsRequirementsOpenGLKHR *r) {
	(void)instance; (void)id;
	r->minApiVersionSupported = XR_MAKE_VERSION(fake.gl_min_major, fake.gl_min_minor, 0);
	r->maxApiVersionSupported = XR_MAKE_VERSION(4, 6, 0);
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_enumerate_refresh_rates(XrSession session, uint32_t capacity, uint32_t *count, float *rates) {
	(void)session;
	fake.enumerate_refresh_calls++;
	*count = fake.refresh_rate_count;
	for (uint32_t i = 0; i < fake.refresh_rate_count && i < capacity; i++) {
		rates[i] = fake.refresh_rates[i];
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_request_refresh_rate(XrSession session, float rate) {
	(void)session;
	fake.request_refresh_calls++;
	fake.requested_refresh = rate;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_get_instance_proc_addr(XrInstance instance, const char *name, PFN_xrVoidFunction *fn) {
	(void)instance;
	*fn = NULL;
	if (strcmp(name, "xrGetOpenGLGraphicsRequirementsKHR") == 0) {
		*fn = (PFN_xrVoidFunction)fake_gl_requirements;
	}
	else if (!fake.refresh_fns_missing && strcmp(name, "xrEnumerateDisplayRefreshRatesFB") == 0) {
		*fn = (PFN_xrVoidFunction)fake_enumerate_refresh_rates;
	}
	else if (!fake.refresh_fns_missing && strcmp(name, "xrRequestDisplayRefreshRateFB") == 0) {
		*fn = (PFN_xrVoidFunction)fake_request_refresh_rate;
	}
	return *fn ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED;
}

static XrResult XRAPI_CALL fake_create_session(XrInstance instance, const XrSessionCreateInfo *info, XrSession *session) {
	(void)instance;
	fake.create_session_calls++;
	fake.session_binding = info->next;
	*session = SESSION;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_destroy_session(XrSession session) {
	(void)session;
	fake.destroy_session_calls++;
	fake_call("destroy_session");
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_create_reference_space(XrSession session, const XrReferenceSpaceCreateInfo *info, XrSpace *space) {
	(void)session;
	int type = (int)info->referenceSpaceType;
	if (type >= 0 && type < 4) {
		fake.space_creates[type]++;
	}
	*space = info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL ? LOCAL_SPACE : VIEW_SPACE;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_destroy_space(XrSpace space) {
	(void)space;
	fake.destroy_space_calls++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_locate_space(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation *location) {
	(void)space; (void)base; (void)time;
	fake.locate_space_calls++;
	location->locationFlags = fake.head_flags;
	location->pose = fake.head_pose;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_swapchain_formats(XrSession session, uint32_t capacity, uint32_t *count, int64_t *formats) {
	(void)session;
	*count = fake.format_count;
	for (uint32_t i = 0; i < fake.format_count && i < capacity; i++) {
		formats[i] = fake.formats[i];
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_create_swapchain(XrSession session, const XrSwapchainCreateInfo *info, XrSwapchain *swapchain) {
	(void)session;
	if (fake.swapchain_creates >= MAX_SWAPCHAINS) {
		return XR_ERROR_LIMIT_REACHED;
	}
	fake.swapchain_info[fake.swapchain_creates] = *info;
	*swapchain = HANDLE(0x100 + fake.swapchain_creates);
	fake.swapchain_creates++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_destroy_swapchain(XrSwapchain swapchain) {
	(void)swapchain;
	fake.swapchain_destroys++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_swapchain_images(XrSwapchain swapchain, uint32_t capacity, uint32_t *count, XrSwapchainImageBaseHeader *images) {
	*count = 3;
	if (capacity == 0) {
		return XR_SUCCESS;
	}
	XrSwapchainImageOpenGLKHR *gl = (XrSwapchainImageOpenGLKHR *)images;
	for (uint32_t i = 0; i < 3 && i < capacity; i++) {
		if (gl[i].type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR) {
			return XR_ERROR_VALIDATION_FAILURE;
		}
		gl[i].image = (uint32_t)(((uintptr_t)swapchain & 0xff) * 10 + i);
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_acquire(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo *info, uint32_t *index) {
	(void)swapchain; (void)info;
	*index = (uint32_t)(fake.acquires % 3);
	fake.acquires++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_wait_image(XrSwapchain swapchain, const XrSwapchainImageWaitInfo *info) {
	(void)swapchain; (void)info;
	fake.waits++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_release(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo *info) {
	(void)swapchain; (void)info;
	fake.releases++;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_poll_event(XrInstance instance, XrEventDataBuffer *buffer) {
	(void)instance;
	if (fake.queue_head == fake.queue_tail) {
		return XR_EVENT_UNAVAILABLE;
	}
	*buffer = fake.queue[fake.queue_head++ % QUEUE_MAX];
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_begin_session(XrSession session, const XrSessionBeginInfo *info) {
	(void)session;
	fake.begin_session_calls++;
	fake.begin_session_view_config = info->primaryViewConfigurationType;
	fake_call("begin_session");
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_end_session(XrSession session) {
	(void)session;
	fake.end_session_calls++;
	fake_call("end_session");
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_request_exit(XrSession session) {
	(void)session;
	fake.request_exit_calls++;
	if (fake.stop_on_exit_request) {
		push_state(XR_SESSION_STATE_STOPPING);
	}
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_wait_frame(XrSession session, const XrFrameWaitInfo *info, XrFrameState *state) {
	(void)session; (void)info;
	fake.wait_frame_calls++;
	if (XR_FAILED(fake.wait_frame_result)) {
		return fake.wait_frame_result;
	}
	state->predictedDisplayTime = fake.time;
	state->predictedDisplayPeriod = FRAME_PERIOD_NS;
	state->shouldRender = fake.should_render ? XR_TRUE : XR_FALSE;
	fake.time += FRAME_PERIOD_NS;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_begin_frame(XrSession session, const XrFrameBeginInfo *info) {
	(void)session; (void)info;
	fake.begin_frame_calls++;
	return fake.begin_frame_result;
}

static XrResult XRAPI_CALL fake_end_frame(XrSession session, const XrFrameEndInfo *info) {
	(void)session;
	fake.end_frame_calls++;
	fake.last_layer_count = info->layerCount;
	fake.last_layer_type = info->layerCount ? info->layers[0]->type : 0;
	fake.last_end_time = info->displayTime;
	fake.last_blend = info->environmentBlendMode;
	return XR_SUCCESS;
}

static XrResult XRAPI_CALL fake_locate_views(XrSession session, const XrViewLocateInfo *info, XrViewState *state, uint32_t capacity, uint32_t *count, XrView *views) {
	(void)session; (void)info;
	fake.locate_views_calls++;
	state->viewStateFlags = fake.view_flags;
	*count = 2;
	for (uint32_t i = 0; i < 2 && i < capacity; i++) {
		views[i].pose.orientation.w = 1;
		views[i].pose.position.x = i == 0 ? -0.032f : 0.032f;
		views[i].pose.position.y = 1.6f;
		views[i].fov.angleLeft = -1.028f;
		views[i].fov.angleRight = 0.885f;
		views[i].fov.angleUp = 0.864f;
		views[i].fov.angleDown = -1.048f;
	}
	return XR_SUCCESS;
}

static void fake_api(vr_xr_api_t *api) {
	memset(api, 0, sizeof(*api));
	api->get_instance_proc_addr = fake_get_instance_proc_addr;
	api->enumerate_instance_extension_properties = fake_enumerate_extensions;
	api->create_instance = fake_create_instance;
	api->destroy_instance = fake_destroy_instance;
	api->get_instance_properties = fake_instance_properties;
	api->result_to_string = fake_result_to_string;
	api->get_system = fake_get_system;
	api->get_system_properties = fake_system_properties;
	api->enumerate_view_configuration_views = fake_view_configuration_views;
	api->enumerate_environment_blend_modes = fake_blend_modes;
	api->create_session = fake_create_session;
	api->destroy_session = fake_destroy_session;
	api->create_reference_space = fake_create_reference_space;
	api->destroy_space = fake_destroy_space;
	api->locate_space = fake_locate_space;
	api->enumerate_swapchain_formats = fake_swapchain_formats;
	api->create_swapchain = fake_create_swapchain;
	api->destroy_swapchain = fake_destroy_swapchain;
	api->enumerate_swapchain_images = fake_swapchain_images;
	api->acquire_swapchain_image = fake_acquire;
	api->wait_swapchain_image = fake_wait_image;
	api->release_swapchain_image = fake_release;
	api->poll_event = fake_poll_event;
	api->begin_session = fake_begin_session;
	api->end_session = fake_end_session;
	api->request_exit_session = fake_request_exit;
	api->wait_frame = fake_wait_frame;
	api->begin_frame = fake_begin_frame;
	api->end_frame = fake_end_frame;
	api->locate_views = fake_locate_views;
}


// -----------------------------------------------------------------------------
// Helpers

static char last_log[512];
static int log_lines;
static char log_text[8192];

static void log_sink(const char *line) {
	snprintf(last_log, sizeof(last_log), "%s", line);
	log_lines++;
	if (strlen(log_text) + strlen(line) + 2 < sizeof(log_text)) {
		strcat(log_text, line);
		strcat(log_text, "\n");
	}
}

static int binding_marker;

// A host with a session, the way vr.c makes one
static bool make_host(vr_xr_t *xr, float scale, int refresh_rate, bool force_render) {
	vr_xr_api_t api;
	fake_api(&api);
	vr_xr_config_t config = {
		.app_name = "WipEoutVR test",
		.resolution_scale = scale,
		.refresh_rate = refresh_rate,
		.force_render = force_render
	};
	return vr_xr_instance_create(xr, &api, &config) && vr_xr_session_create(xr, &binding_marker);
}

// Steps the session to FOCUSED the way the Frame's runtime does
static void go_focused(vr_xr_t *xr) {
	push_state(XR_SESSION_STATE_IDLE);
	push_state(XR_SESSION_STATE_READY);
	push_state(XR_SESSION_STATE_SYNCHRONIZED);
	push_state(XR_SESSION_STATE_VISIBLE);
	push_state(XR_SESSION_STATE_FOCUSED);
	vr_xr_poll_events(xr);
}

static bool one_frame(vr_xr_t *xr, const XrCompositionLayerBaseHeader *const *layers, uint32_t count) {
	if (!vr_xr_frame_begin(xr)) {
		return false;
	}
	vr_xr_frame_end(xr, layers, count);
	return true;
}


// -----------------------------------------------------------------------------
// Tests

static void test_pure_helpers(void) {
	CHECK_INT(vr_xr_scaled_size(1728, 8192, 1.0f), 1728);
	CHECK_INT(vr_xr_scaled_size(1728, 8192, 0.75f), 1296);
	CHECK_INT(vr_xr_scaled_size(1728, 8192, 1.5f), 2592);
	CHECK_INT(vr_xr_scaled_size(1728, 2000, 1.5f), 2000); // clamped to the maximum
	CHECK_INT(vr_xr_scaled_size(1728, 8192, 0.0f), 1728); // a bad scale means 1
	CHECK_INT(vr_xr_scaled_size(1, 8192, 0.75f), 1);
	CHECK_INT(vr_xr_scaled_size(0, 8192, 1.0f), 1);

	float rates[] = { 72, 90, 144 };
	CHECK_INT(vr_xr_choose_refresh_rate(rates, 3, 90), 90);
	CHECK_INT(vr_xr_choose_refresh_rate(rates, 3, 100), 90);
	CHECK_INT(vr_xr_choose_refresh_rate(rates, 3, 130), 144);
	CHECK_INT(vr_xr_choose_refresh_rate(rates, 3, 30), 72);
	float two[] = { 72, 144 };
	CHECK_INT(vr_xr_choose_refresh_rate(two, 2, 108), 72); // a tie goes to the lower rate
	CHECK_INT(vr_xr_choose_refresh_rate(rates, 0, 90), 0);

	bool srgb;
	int64_t both[] = { VR_GL_RGBA8, VR_GL_SRGB8, VR_GL_SRGB8_ALPHA8 };
	CHECK_INT(vr_xr_choose_color_format(both, 3, &srgb), VR_GL_SRGB8_ALPHA8);
	CHECK(srgb);
	int64_t frame[] = { VR_GL_SRGB8, VR_GL_SRGB8_ALPHA8 }; // what the Frame offers
	CHECK_INT(vr_xr_choose_color_format(frame, 2, &srgb), VR_GL_SRGB8_ALPHA8);
	int64_t no_alpha[] = { VR_GL_RGBA8, VR_GL_SRGB8 };
	CHECK_INT(vr_xr_choose_color_format(no_alpha, 2, &srgb), VR_GL_SRGB8);
	CHECK(srgb);
	int64_t linear[] = { VR_GL_RGBA8 };
	CHECK_INT(vr_xr_choose_color_format(linear, 1, &srgb), VR_GL_RGBA8);
	CHECK(!srgb);
	int64_t other[] = { 0x1234 };
	CHECK_INT(vr_xr_choose_color_format(other, 1, &srgb), 0);
}

static void test_instance_negotiation(void) {
	fake_reset();
	vr_xr_t xr;
	vr_xr_api_t api;
	fake_api(&api);
	vr_xr_config_t config = { .app_name = "t", .resolution_scale = 1.0f, .refresh_rate = 90 };

	CHECK(vr_xr_instance_create(&xr, &api, &config));
	CHECK(xr.ext_frame_controller);
	CHECK(xr.ext_refresh_rate);
	CHECK_INT(fake.enabled_extension_count, 3);
	CHECK_STR(fake.enabled_extensions[0], "XR_KHR_opengl_enable");
	CHECK_STR(fake.enabled_extensions[1], "XR_VALVE_frame_controller_interaction");
	CHECK_STR(fake.enabled_extensions[2], "XR_FB_display_refresh_rate");
	CHECK_STR(xr.runtime_name, "Fake Runtime");
	CHECK_INT(xr.runtime_version_major, 2);
	CHECK_INT(xr.runtime_version_minor, 17);
	CHECK_STR(xr.system_name, "Fake HMD");
	CHECK_INT(XR_VERSION_MAJOR(xr.gl_min_version), 4);
	CHECK_INT(XR_VERSION_MINOR(xr.gl_min_version), 3);
	CHECK_INT(xr.eye_recommended[0][0], 1728);
	CHECK_INT(xr.blend_mode, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
	vr_xr_destroy(&xr);
	CHECK_INT(fake.destroy_instance_calls, 1);

	// Optional extensions are optional
	fake_reset();
	fake.offers_valve = false;
	fake.offers_refresh = false;
	CHECK(vr_xr_instance_create(&xr, &api, &config));
	CHECK(!xr.ext_frame_controller);
	CHECK(!xr.ext_refresh_rate);
	CHECK_INT(fake.enabled_extension_count, 1);
	vr_xr_destroy(&xr);

	// An extension that is offered but whose functions are not: off
	fake_reset();
	fake.refresh_fns_missing = true;
	CHECK(vr_xr_instance_create(&xr, &api, &config));
	CHECK(!xr.ext_refresh_rate);
	vr_xr_destroy(&xr);

	// No OpenGL: no instance at all
	fake_reset();
	fake.offers_gl = false;
	CHECK(!vr_xr_instance_create(&xr, &api, &config));
	CHECK_INT(fake.create_instance_calls, 0);

	// Only ALPHA_BLEND offered: it is used, with a log line
	fake_reset();
	fake.blend_modes[0] = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
	fake.blend_count = 1;
	CHECK(vr_xr_instance_create(&xr, &api, &config));
	CHECK_INT(xr.blend_mode, XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
	vr_xr_destroy(&xr);
}

static void test_session_and_swapchains(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 90, false));
	CHECK(fake.session_binding == &binding_marker);
	CHECK_INT(fake.space_creates[XR_REFERENCE_SPACE_TYPE_LOCAL], 1);
	CHECK_INT(fake.space_creates[XR_REFERENCE_SPACE_TYPE_VIEW], 1);
	CHECK_INT(xr.color_format, VR_GL_SRGB8_ALPHA8);
	CHECK(xr.color_srgb);
	CHECK_INT(fake.swapchain_creates, 2);
	CHECK_INT(fake.swapchain_info[0].format, VR_GL_SRGB8_ALPHA8);
	CHECK_INT(fake.swapchain_info[0].width, 1728);
	CHECK_INT(fake.swapchain_info[0].height, 1728);
	CHECK_INT(fake.swapchain_info[1].width, 1728);
	CHECK_INT(fake.swapchain_info[0].sampleCount, 1);
	CHECK_INT(xr.eye[0].image_count, 3);
	CHECK(xr.eye[0].handle != XR_NULL_HANDLE);
	CHECK(xr.eye[0].handle != xr.eye[1].handle);
	CHECK_INT(xr.eye[1].images[2].image, 12); // the fake: (handle & 0xff) * 10 + index

	// acquire and release
	uint32_t index = 99;
	CHECK(vr_xr_swapchain_acquire(&xr, &xr.eye[0], &index));
	CHECK_INT(index, 0);
	CHECK(xr.eye[0].acquired);
	CHECK(vr_xr_swapchain_release(&xr, &xr.eye[0]));
	CHECK(!xr.eye[0].acquired);
	CHECK(!vr_xr_swapchain_release(&xr, &xr.eye[0])); // nothing acquired
	CHECK_INT(fake.acquires, 1);
	CHECK_INT(fake.waits, 1);
	CHECK_INT(fake.releases, 1);
	vr_xr_destroy(&xr);
	CHECK_INT(fake.swapchain_destroys, 2);
	CHECK_INT(fake.destroy_space_calls, 2);
	CHECK_INT(fake.destroy_session_calls, 1);

	// resolution_scale
	fake_reset();
	CHECK(make_host(&xr, 0.75f, 0, false));
	CHECK_INT(fake.swapchain_info[0].width, 1296);
	CHECK_INT(fake.swapchain_info[1].height, 1296);
	vr_xr_destroy(&xr);

	fake_reset();
	CHECK(make_host(&xr, 1.5f, 0, false));
	CHECK_INT(fake.swapchain_info[0].width, 2592);
	vr_xr_destroy(&xr);

	// no sRGB format at all: RGBA8 with a warning
	fake_reset();
	fake.formats[0] = VR_GL_RGBA8;
	fake.format_count = 1;
	CHECK(make_host(&xr, 1.0f, 0, false));
	CHECK(!xr.color_srgb);
	CHECK_INT(xr.color_format, VR_GL_RGBA8);
	vr_xr_destroy(&xr);

	// no usable format: the session cannot be set up
	fake_reset();
	fake.formats[0] = 0x1234;
	fake.format_count = 1;
	CHECK(!make_host(&xr, 1.0f, 0, false));
	vr_xr_destroy(&xr);
}

static void test_lifecycle(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, false));

	// Before READY there is nothing to wait for
	CHECK(!vr_xr_frame_begin(&xr));
	CHECK_INT(fake.wait_frame_calls, 0);
	push_state(XR_SESSION_STATE_IDLE);
	vr_xr_poll_events(&xr);
	CHECK(!xr.session_running);
	CHECK_INT(xr.state, XR_SESSION_STATE_IDLE);
	CHECK(!vr_xr_frame_begin(&xr));

	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK(xr.session_running);
	CHECK_INT(fake.begin_session_calls, 1);
	CHECK_INT(fake.begin_session_view_config, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);

	// SYNCHRONIZED and VISIBLE: running, not focused, so the sim is frozen
	push_state(XR_SESSION_STATE_SYNCHRONIZED);
	push_state(XR_SESSION_STATE_VISIBLE);
	vr_xr_poll_events(&xr);
	CHECK(xr.session_running);
	CHECK(!xr.focused);
	CHECK(vr_xr_frame_begin(&xr));
	CHECK(xr.frame.should_render);
	CHECK(!vr_xr_sim_active(&xr));
	vr_xr_frame_end(&xr, NULL, 0);

	push_state(XR_SESSION_STATE_FOCUSED);
	vr_xr_poll_events(&xr);
	CHECK(xr.focused);
	CHECK_INT(xr.focus_gains, 1);
	CHECK(vr_xr_frame_begin(&xr));
	CHECK(vr_xr_sim_active(&xr));
	vr_xr_frame_end(&xr, NULL, 0);

	// A second READY while running does not begin again
	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK_INT(fake.begin_session_calls, 1);

	// Losing focus and getting it back
	push_state(XR_SESSION_STATE_VISIBLE);
	vr_xr_poll_events(&xr);
	CHECK(!xr.focused);
	push_state(XR_SESSION_STATE_FOCUSED);
	vr_xr_poll_events(&xr);
	CHECK_INT(xr.focus_gains, 2);
	vr_xr_destroy(&xr);
}

static void test_frame_data(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);

	CHECK(vr_xr_frame_begin(&xr));
	CHECK(xr.frame_open);
	CHECK_INT(xr.frame.predicted_display_time, 1000000000);
	CHECK_INT(xr.frame.predicted_display_period, FRAME_PERIOD_NS);
	CHECK(xr.frame.should_render);
	CHECK(xr.frame.views_valid);
	CHECK_INT(fake.locate_views_calls, 1);
	CHECK_NEAR(xr.frame.views[0].pose.position[0], -0.032, 1e-6);
	CHECK_NEAR(xr.frame.views[1].pose.position[0], 0.032, 1e-6);
	CHECK_NEAR(xr.frame.views[0].pose.position[1], 1.6, 1e-6);
	CHECK_NEAR(xr.frame.views[0].pose.orientation[3], 1.0, 1e-6);
	CHECK_NEAR(xr.frame.views[0].fov[0], -1.028, 1e-6);
	CHECK_NEAR(xr.frame.views[0].fov[1], 0.885, 1e-6);
	CHECK_NEAR(xr.frame.views[0].fov[2], 0.864, 1e-6);
	CHECK_NEAR(xr.frame.views[0].fov[3], -1.048, 1e-6);

	// A projection layer from the located views, and a quad layer
	XrCompositionLayerProjection projection;
	XrCompositionLayerProjectionView views[2];
	vr_xr_projection_layer(&xr, &projection, views);
	CHECK_INT(projection.type, XR_TYPE_COMPOSITION_LAYER_PROJECTION);
	CHECK(projection.space == LOCAL_SPACE);
	CHECK_INT(projection.viewCount, 2);
	CHECK_NEAR(views[1].pose.position.x, 0.032, 1e-6);
	CHECK_NEAR(views[0].fov.angleDown, -1.048, 1e-6);
	CHECK(views[0].subImage.swapchain == xr.eye[0].handle);
	CHECK_INT(views[0].subImage.imageRect.extent.width, 1728);

	vr_pose_t pose = { .orientation = {0, 0, 0, 1}, .position = {0, 1.6f, -2} };
	XrCompositionLayerQuad quad;
	vr_swapchain_t panel = { .handle = HANDLE(0x500), .width = 1280, .height = 720 };
	vr_xr_quad_layer(&xr, &quad, &panel, &pose, 2.4f);
	CHECK_INT(quad.type, XR_TYPE_COMPOSITION_LAYER_QUAD);
	CHECK(quad.space == LOCAL_SPACE);
	CHECK_INT(quad.eyeVisibility, XR_EYE_VISIBILITY_BOTH);
	CHECK_NEAR(quad.size.width, 2.4, 1e-6);
	CHECK_NEAR(quad.size.height, 1.35, 1e-6);
	CHECK_NEAR(quad.pose.position.z, -2.0, 1e-6);
	CHECK_INT(quad.subImage.imageRect.extent.width, 1280);

	const XrCompositionLayerBaseHeader *layers[2] = {
		(const XrCompositionLayerBaseHeader *)&projection,
		(const XrCompositionLayerBaseHeader *)&quad
	};
	CHECK(vr_xr_frame_end(&xr, layers, 2));
	CHECK(!xr.frame_open);
	CHECK_INT(fake.last_layer_count, 2);
	CHECK_INT(fake.last_layer_type, XR_TYPE_COMPOSITION_LAYER_PROJECTION);
	CHECK_INT(fake.last_end_time, 1000000000);
	CHECK_INT(fake.last_blend, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
	CHECK(!vr_xr_frame_end(&xr, layers, 2)); // already ended

	// Invalid tracking: the frame still runs, the views are not usable
	fake.view_flags = XR_VIEW_STATE_ORIENTATION_VALID_BIT;
	CHECK(vr_xr_frame_begin(&xr));
	CHECK(!xr.frame.views_valid);
	vr_xr_frame_end(&xr, NULL, 0);

	// The head in LOCAL space
	vr_pose_t head;
	CHECK(vr_xr_frame_begin(&xr));
	CHECK(vr_xr_locate_head(&xr, &head));
	CHECK_NEAR(head.position[1], 1.6, 1e-6);
	fake.head_flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	CHECK(!vr_xr_locate_head(&xr, &head));
	vr_xr_frame_end(&xr, NULL, 0);
	vr_xr_destroy(&xr);
}

static void test_standby_keeps_loop_alive(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, false));

	// A headset in standby: READY and SYNCHRONIZED, and the runtime says
	// shouldRender = false. The loop must keep waiting, beginning and ending frames.
	push_state(XR_SESSION_STATE_READY);
	push_state(XR_SESSION_STATE_SYNCHRONIZED);
	vr_xr_poll_events(&xr);
	fake.should_render = false;

	XrCompositionLayerQuad quad = { .type = XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *layers[1] = { (const XrCompositionLayerBaseHeader *)&quad };

	for (int i = 0; i < 100; i++) {
		vr_xr_poll_events(&xr);
		CHECK(one_frame(&xr, layers, 1));
		CHECK(!xr.frame.should_render);
		CHECK(!xr.frame.views_valid);
		CHECK(!vr_xr_sim_active(&xr));
		CHECK_NEAR(xr.frame_dt, 0.0, 1e-12);
		CHECK_INT(fake.last_layer_count, 0); // nothing is submitted in standby
		CHECK(xr.session_running);
		CHECK(!xr.exit_requested);
	}
	CHECK_INT(fake.wait_frame_calls, 100);
	CHECK_INT(fake.begin_frame_calls, 100);
	CHECK_INT(fake.end_frame_calls, 100);
	CHECK_INT(fake.locate_views_calls, 0);
	CHECK_INT(xr.frames_begun, 100);
	CHECK_INT(xr.frames_ended, 100);

	// The wearer puts it on: rendering starts again, layers go through
	fake.should_render = true;
	push_state(XR_SESSION_STATE_VISIBLE);
	push_state(XR_SESSION_STATE_FOCUSED);
	vr_xr_poll_events(&xr);
	CHECK(one_frame(&xr, layers, 1));
	CHECK(xr.frame.should_render);
	CHECK(xr.frame.views_valid);
	CHECK(vr_xr_sim_active(&xr));
	CHECK_INT(fake.last_layer_count, 1);
	CHECK_INT(fake.locate_views_calls, 1);
	CHECK_NEAR(xr.frame_dt, FRAME_PERIOD_NS * 1e-9, 1e-9); // time advanced all through standby

	// and back to standby
	fake.should_render = false;
	push_state(XR_SESSION_STATE_SYNCHRONIZED);
	vr_xr_poll_events(&xr);
	CHECK(one_frame(&xr, layers, 1));
	CHECK(!vr_xr_sim_active(&xr));
	CHECK_INT(fake.last_layer_count, 0);
	vr_xr_destroy(&xr);
}

static void test_force_render_in_standby(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, true));
	push_state(XR_SESSION_STATE_READY);
	push_state(XR_SESSION_STATE_SYNCHRONIZED);
	vr_xr_poll_events(&xr);
	fake.should_render = false;

	XrCompositionLayerQuad quad = { .type = XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *layers[1] = { (const XrCompositionLayerBaseHeader *)&quad };
	CHECK(one_frame(&xr, layers, 1));
	CHECK(!xr.frame.should_render);
	CHECK(xr.frame.views_valid); // synthetic head at 1.6 m
	CHECK_NEAR(xr.frame.views[0].pose.position[1], 1.6, 1e-6);
	CHECK_NEAR(xr.frame.views[1].pose.position[0] - xr.frame.views[0].pose.position[0], 0.064, 1e-6);
	CHECK_INT(fake.last_layer_count, 1);
	CHECK(!vr_xr_sim_active(&xr)); // the sim still does not run
	CHECK_INT(fake.locate_views_calls, 0);
	vr_xr_destroy(&xr);
}

static void test_stop_and_restart(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	CHECK(one_frame(&xr, NULL, 0));
	CHECK(one_frame(&xr, NULL, 0));
	CHECK(xr.frame_dt > 0);

	push_state(XR_SESSION_STATE_STOPPING);
	vr_xr_poll_events(&xr);
	CHECK(!xr.session_running);
	CHECK_INT(fake.end_session_calls, 1);
	int waits = fake.wait_frame_calls;
	CHECK(!vr_xr_frame_begin(&xr));
	CHECK_INT(fake.wait_frame_calls, waits); // no waiting while stopped
	CHECK(!xr.exit_requested);

	push_state(XR_SESSION_STATE_IDLE);
	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK(xr.session_running);
	CHECK_INT(fake.begin_session_calls, 2);
	push_state(XR_SESSION_STATE_SYNCHRONIZED);
	push_state(XR_SESSION_STATE_VISIBLE);
	push_state(XR_SESSION_STATE_FOCUSED);
	vr_xr_poll_events(&xr);
	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.0, 1e-12); // the clock restarted, no dt across the stop
	vr_xr_frame_end(&xr, NULL, 0);

	// STOPPING twice only ends the session once
	push_state(XR_SESSION_STATE_STOPPING);
	push_state(XR_SESSION_STATE_STOPPING);
	vr_xr_poll_events(&xr);
	CHECK_INT(fake.end_session_calls, 2);
	vr_xr_destroy(&xr);
}

static void test_exit_paths(void) {
	vr_xr_t xr;

	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	push_state(XR_SESSION_STATE_STOPPING);
	push_state(XR_SESSION_STATE_EXITING);
	vr_xr_poll_events(&xr);
	CHECK(xr.exit_requested);
	CHECK(!xr.session_running);
	vr_xr_destroy(&xr);

	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	push_state(XR_SESSION_STATE_LOSS_PENDING);
	vr_xr_poll_events(&xr);
	CHECK(xr.exit_requested);
	vr_xr_destroy(&xr);

	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	CHECK(!xr.exit_requested);
	push_instance_loss();
	vr_xr_poll_events(&xr);
	CHECK(xr.exit_requested);
	vr_xr_destroy(&xr);

	// An app-requested exit (Quit to Steam): ask, then the runtime says STOPPING
	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	vr_xr_request_exit(&xr);
	CHECK_INT(fake.request_exit_calls, 1);
	vr_xr_poll_events(&xr);
	CHECK(!xr.session_running);
	CHECK_INT(fake.end_session_calls, 1);
	vr_xr_request_exit(&xr); // not running: no second request
	CHECK_INT(fake.request_exit_calls, 1);
	vr_xr_destroy(&xr);
}

static void test_reference_space_change(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	CHECK_INT(xr.recentre_requests, 0);

	push_reference_space_change();
	vr_xr_poll_events(&xr);
	CHECK_INT(xr.recentre_requests, 1);
	CHECK(xr.session_running);
	CHECK(!xr.exit_requested);

	push_reference_space_change();
	push_reference_space_change();
	vr_xr_poll_events(&xr);
	CHECK_INT(xr.recentre_requests, 3);

	// and the frame loop is undisturbed
	CHECK(one_frame(&xr, NULL, 0));
	vr_xr_destroy(&xr);
}

static void test_dt_from_display_time(void) {
	fake_reset();
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);

	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.0, 1e-12); // the first frame has no predecessor
	vr_xr_frame_end(&xr, NULL, 0);

	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.013888889, 1e-9);
	vr_xr_frame_end(&xr, NULL, 0);

	// a long gap is clamped to the game's 0.1 s
	fake.time += 2000000000;
	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.1, 1e-12);
	vr_xr_frame_end(&xr, NULL, 0);

	// 90 Hz: 11.1 ms after the previous frame
	fake.time = xr.frame.predicted_display_time + 11111111;
	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.011111111, 1e-9);
	vr_xr_frame_end(&xr, NULL, 0);

	// no focus: dt 0, but the time base keeps moving
	push_state(XR_SESSION_STATE_VISIBLE);
	vr_xr_poll_events(&xr);
	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.0, 1e-12);
	vr_xr_frame_end(&xr, NULL, 0);
	push_state(XR_SESSION_STATE_FOCUSED);
	vr_xr_poll_events(&xr);
	CHECK(vr_xr_frame_begin(&xr));
	CHECK_NEAR(xr.frame_dt, 0.013888889, 1e-9);
	vr_xr_frame_end(&xr, NULL, 0);
	vr_xr_destroy(&xr);
}

static void test_refresh_rate_request(void) {
	vr_xr_t xr;

	fake_reset();
	CHECK(make_host(&xr, 1.0f, 90, false));
	CHECK_INT(fake.request_refresh_calls, 0); // not before the session runs
	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK_INT(fake.enumerate_refresh_calls, 1);
	CHECK_INT(fake.request_refresh_calls, 1);
	CHECK_NEAR(fake.requested_refresh, 90, 1e-6);
	CHECK_INT(xr.refresh_rate_chosen, 90);
	vr_xr_destroy(&xr);

	// 100 asked for, 72/90/144 offered: 90
	fake_reset();
	CHECK(make_host(&xr, 1.0f, 100, false));
	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK_NEAR(fake.requested_refresh, 90, 1e-6);
	vr_xr_destroy(&xr);

	// not offered by the runtime: no request
	fake_reset();
	fake.offers_refresh = false;
	CHECK(make_host(&xr, 1.0f, 90, false));
	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK_INT(fake.request_refresh_calls, 0);
	CHECK(xr.session_running);
	vr_xr_destroy(&xr);

	// not wanted: no request
	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	push_state(XR_SESSION_STATE_READY);
	vr_xr_poll_events(&xr);
	CHECK_INT(fake.request_refresh_calls, 0);
	vr_xr_destroy(&xr);
}

static void test_runtime_failures(void) {
	vr_xr_t xr;

	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	fake.wait_frame_result = XR_ERROR_RUNTIME_FAILURE;
	CHECK(!vr_xr_frame_begin(&xr));
	CHECK(!xr.exit_requested); // a failed wait is skipped, not fatal
	CHECK(!xr.frame_open);
	fake.wait_frame_result = XR_ERROR_SESSION_LOST;
	CHECK(!vr_xr_frame_begin(&xr));
	CHECK(xr.exit_requested);
	vr_xr_destroy(&xr);

	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	fake.begin_frame_result = XR_ERROR_SESSION_LOST;
	CHECK(!vr_xr_frame_begin(&xr));
	CHECK(xr.exit_requested);
	vr_xr_destroy(&xr);

	// XR_FRAME_DISCARDED is a success: the previous frame was never ended
	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	fake.begin_frame_result = XR_FRAME_DISCARDED;
	CHECK(vr_xr_frame_begin(&xr));
	vr_xr_frame_end(&xr, NULL, 0);
	vr_xr_destroy(&xr);
}

static void test_destroy_order(void) {
	vr_xr_t xr;

	// Destroyed while running: the session is ended first, instance last
	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	go_focused(&xr);
	fake.call_count = 0;
	vr_xr_destroy(&xr);
	CHECK_INT(fake.end_session_calls, 1);
	CHECK_INT(fake.call_count, 3);
	if (fake.call_count == 3) {
		CHECK_STR(fake.calls[0], "end_session");
		CHECK_STR(fake.calls[1], "destroy_session");
		CHECK_STR(fake.calls[2], "destroy_instance");
	}
	CHECK_INT(fake.swapchain_destroys, 2);

	// Destroyed before READY: no xrEndSession
	fake_reset();
	CHECK(make_host(&xr, 1.0f, 0, false));
	vr_xr_destroy(&xr);
	CHECK_INT(fake.end_session_calls, 0);
	CHECK_INT(fake.destroy_session_calls, 1);
}

static void test_log_lines(void) {
	fake_reset();
	log_text[0] = '\0';
	vr_xr_t xr;
	CHECK(make_host(&xr, 1.0f, 90, false));
	go_focused(&xr);
	push_reference_space_change();
	vr_xr_poll_events(&xr);
	CHECK(strstr(log_text, "runtime Fake Runtime 2.17.10") != NULL);
	CHECK(strstr(log_text, "session state READY") != NULL);
	CHECK(strstr(log_text, "session state FOCUSED") != NULL);
	CHECK(strstr(log_text, "reference space change pending") != NULL);
	CHECK(strstr(log_text, "refresh rate: asked for 90 Hz") != NULL);
	vr_xr_destroy(&xr);
}

int main(void) {
	vr_log_set_sink(log_sink);
	RUN(test_pure_helpers);
	RUN(test_instance_negotiation);
	RUN(test_session_and_swapchains);
	RUN(test_lifecycle);
	RUN(test_frame_data);
	RUN(test_standby_keeps_loop_alive);
	RUN(test_force_render_in_standby);
	RUN(test_stop_and_restart);
	RUN(test_exit_paths);
	RUN(test_reference_space_change);
	RUN(test_dt_from_display_time);
	RUN(test_refresh_rate_request);
	RUN(test_runtime_failures);
	RUN(test_destroy_order);
	RUN(test_log_lines);
	return SFVR_TEST_RESULT("vr_xr");
}
