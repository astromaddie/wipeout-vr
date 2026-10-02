#ifndef VR_XR_H
#define VR_XR_H

#include <stdbool.h>
#include <stdint.h>

// Before any OpenXR header: the OpenGL types (XrSwapchainImageOpenGLKHR, the
// requirements) need no platform headers. The Xlib binding is built in vr_gl.c.
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "vr_types.h"
#include "vr_clock.h"

// The OpenXR host: instance, system, session lifecycle, spaces, swapchains and
// the frame loop. It calls the runtime only through vr_xr_api_t, so the
// lifecycle can be tested with a fake runtime. It knows nothing about SDL or
// the GL context; vr_gl.c supplies the graphics binding.

#define XR_VALVE_FRAME_CONTROLLER_EXTENSION_NAME "XR_VALVE_frame_controller_interaction"

#define VR_SWAPCHAIN_MAX_IMAGES 8
#define VR_MAX_REFRESH_RATES 16

#define VR_GL_SRGB8_ALPHA8 0x8C43
#define VR_GL_SRGB8 0x8C41
#define VR_GL_RGBA8 0x8058

typedef struct {
	PFN_xrGetInstanceProcAddr get_instance_proc_addr;
	PFN_xrEnumerateInstanceExtensionProperties enumerate_instance_extension_properties;
	PFN_xrCreateInstance create_instance;
	PFN_xrDestroyInstance destroy_instance;
	PFN_xrGetInstanceProperties get_instance_properties;
	PFN_xrResultToString result_to_string;
	PFN_xrGetSystem get_system;
	PFN_xrGetSystemProperties get_system_properties;
	PFN_xrEnumerateViewConfigurationViews enumerate_view_configuration_views;
	PFN_xrEnumerateEnvironmentBlendModes enumerate_environment_blend_modes;
	PFN_xrCreateSession create_session;
	PFN_xrDestroySession destroy_session;
	PFN_xrCreateReferenceSpace create_reference_space;
	PFN_xrDestroySpace destroy_space;
	PFN_xrLocateSpace locate_space;
	PFN_xrEnumerateSwapchainFormats enumerate_swapchain_formats;
	PFN_xrCreateSwapchain create_swapchain;
	PFN_xrDestroySwapchain destroy_swapchain;
	PFN_xrEnumerateSwapchainImages enumerate_swapchain_images;
	PFN_xrAcquireSwapchainImage acquire_swapchain_image;
	PFN_xrWaitSwapchainImage wait_swapchain_image;
	PFN_xrReleaseSwapchainImage release_swapchain_image;
	PFN_xrPollEvent poll_event;
	PFN_xrBeginSession begin_session;
	PFN_xrEndSession end_session;
	PFN_xrRequestExitSession request_exit_session;
	PFN_xrWaitFrame wait_frame;
	PFN_xrBeginFrame begin_frame;
	PFN_xrEndFrame end_frame;
	PFN_xrLocateViews locate_views;

	// Extension functions. vr_xr_instance_create() fills these through
	// get_instance_proc_addr once the instance exists.
	PFN_xrGetOpenGLGraphicsRequirementsKHR get_opengl_graphics_requirements;
	PFN_xrEnumerateDisplayRefreshRatesFB enumerate_display_refresh_rates;
	PFN_xrRequestDisplayRefreshRateFB request_display_refresh_rate;
} vr_xr_api_t;

// The runtime through the OpenXR loader (vr_xr_loader.c).
void vr_xr_api_loader(vr_xr_api_t *api);

typedef struct {
	const char *app_name;
	float resolution_scale; // of the runtime's recommended eye size
	int refresh_rate; // Hz to request with XR_FB_display_refresh_rate; 0 = do not
	bool force_render; // submit layers even when shouldRender is false (diagnostics)
} vr_xr_config_t;

typedef struct {
	XrSwapchain handle;
	uint32_t width;
	uint32_t height;
	uint32_t image_count;
	XrSwapchainImageOpenGLKHR images[VR_SWAPCHAIN_MAX_IMAGES];
	bool acquired;
	uint32_t acquired_index;
} vr_swapchain_t;

typedef struct {
	vr_xr_api_t api;
	vr_xr_config_t config;

	XrInstance instance;
	XrSystemId system_id;
	XrSession session;
	XrSpace local_space;
	XrSpace view_space;
	XrEnvironmentBlendMode blend_mode;

	char runtime_name[XR_MAX_RUNTIME_NAME_SIZE];
	uint32_t runtime_version_major;
	uint32_t runtime_version_minor;
	uint32_t runtime_version_patch;
	char system_name[XR_MAX_SYSTEM_NAME_SIZE];
	XrVersion gl_min_version;
	XrVersion gl_max_version;

	bool ext_frame_controller; // XR_VALVE_frame_controller_interaction is enabled
	bool ext_refresh_rate; // XR_FB_display_refresh_rate is enabled
	int refresh_rate_chosen; // Hz, 0 when none was requested

	// Session lifecycle. `state` is the last XrSessionState the runtime sent.
	XrSessionState state;
	bool session_running; // between xrBeginSession and xrEndSession
	bool focused;
	bool exit_requested; // EXITING, LOSS_PENDING or instance loss: leave the loop
	uint32_t focus_gains; // times FOCUSED was entered
	uint32_t recentre_requests; // XrEventDataReferenceSpaceChangePending events seen

	// Eye swapchains, recommended size times resolution_scale
	int64_t color_format;
	bool color_srgb;
	uint32_t eye_recommended[2][2]; // width, height
	uint32_t eye_max[2][2];
	vr_swapchain_t eye[2];

	// The current frame; valid between vr_xr_frame_begin() and vr_xr_frame_end()
	vr_frame_t frame;
	bool frame_open;
	vr_clock_t clock;
	double frame_dt; // seconds, from the change in predictedDisplayTime; 0 when the sim is frozen
	uint64_t frames_begun;
	uint64_t frames_ended;
} vr_xr_t;

// Pure: the swapchain size for a recommended and a maximum size, scaled and clamped to 1..max.
uint32_t vr_xr_scaled_size(uint32_t recommended, uint32_t max, float scale);

// Pure: picks the rate to request from the enumerated ones: the nearest to
// `wanted`, the lower one on a tie. 0 when there are none.
int vr_xr_choose_refresh_rate(const float *rates, uint32_t count, int wanted);

// Pure: SRGB8_ALPHA8, else SRGB8, else RGBA8 (then *srgb is false); 0 when none is offered.
int64_t vr_xr_choose_color_format(const int64_t *formats, uint32_t count, bool *srgb);

// Creates the instance with XR_KHR_opengl_enable plus whichever optional
// extensions the runtime offers, picks the HMD system and its view size, and
// reads the GL requirements. Call vr_xr_session_create() next. On failure the
// instance is destroyed again and false returned.
bool vr_xr_instance_create(vr_xr_t *xr, const vr_xr_api_t *api, const vr_xr_config_t *config);

// Session with the given XrGraphicsBindingOpenGL*KHR, LOCAL and VIEW spaces,
// the colour format and the two eye swapchains.
bool vr_xr_session_create(vr_xr_t *xr, const void *graphics_binding);

// Ends the session if it is still running, destroys everything created above.
void vr_xr_destroy(vr_xr_t *xr);

// Swapchains
bool vr_xr_swapchain_create(vr_xr_t *xr, vr_swapchain_t *sc, uint32_t width, uint32_t height);
void vr_xr_swapchain_destroy(vr_xr_t *xr, vr_swapchain_t *sc);
// Acquire and wait; *index is the image to render into. Release when done.
bool vr_xr_swapchain_acquire(vr_xr_t *xr, vr_swapchain_t *sc, uint32_t *index);
bool vr_xr_swapchain_release(vr_xr_t *xr, vr_swapchain_t *sc);

// Drains the runtime's event queue and runs the session state machine:
//   READY: xrBeginSession (PRIMARY_STEREO) and the refresh-rate request.
//   STOPPING: xrEndSession.
//   EXITING, LOSS_PENDING, instance loss: exit_requested.
//   reference-space change pending: recentre_requests++.
// SYNCHRONIZED and VISIBLE keep the session running. SYNCHRONIZED is also what
// a headset in standby sits in.
void vr_xr_poll_events(vr_xr_t *xr);

// Asks the runtime to end the session (a clean exit); keep polling events
// until session_running is false.
void vr_xr_request_exit(vr_xr_t *xr);

// xrWaitFrame, xrBeginFrame and, when shouldRender, xrLocateViews at the
// predicted display time. Returns false when no frame was begun: the session
// is not running, or the runtime failed the wait or begin. When it returns
// true, vr_xr_frame_end() must follow. Updates `frame` and `frame_dt`.
bool vr_xr_frame_begin(vr_xr_t *xr);

// xrEndFrame with OPAQUE blend. With shouldRender false (standby) no layers
// are submitted, unless config.force_render is set.
bool vr_xr_frame_end(vr_xr_t *xr, const XrCompositionLayerBaseHeader *const *layers, uint32_t layer_count);

// The sim advances: the session runs, shouldRender is true and the app has focus.
bool vr_xr_sim_active(const vr_xr_t *xr);

// Where the head is, in LOCAL space, at the frame's predicted display time.
bool vr_xr_locate_head(vr_xr_t *xr, vr_pose_t *head);

// Layers for vr_xr_frame_end(), in LOCAL space.
void vr_xr_quad_layer(const vr_xr_t *xr, XrCompositionLayerQuad *layer, const vr_swapchain_t *sc, const vr_pose_t *pose, float width_m);
void vr_xr_projection_layer(const vr_xr_t *xr, XrCompositionLayerProjection *layer, XrCompositionLayerProjectionView views[2]);

const char *vr_xr_state_name(XrSessionState state);

#endif
