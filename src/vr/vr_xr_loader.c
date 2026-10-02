#include <string.h>

#include "vr_xr.h"

// The real runtime, through the statically linked OpenXR loader. Tests do not
// compile this file; they pass a fake table instead.

void vr_xr_api_loader(vr_xr_api_t *api) {
	memset(api, 0, sizeof(*api));
	api->get_instance_proc_addr = xrGetInstanceProcAddr;
	api->enumerate_instance_extension_properties = xrEnumerateInstanceExtensionProperties;
	api->create_instance = xrCreateInstance;
	api->destroy_instance = xrDestroyInstance;
	api->get_instance_properties = xrGetInstanceProperties;
	api->result_to_string = xrResultToString;
	api->get_system = xrGetSystem;
	api->get_system_properties = xrGetSystemProperties;
	api->enumerate_view_configuration_views = xrEnumerateViewConfigurationViews;
	api->enumerate_environment_blend_modes = xrEnumerateEnvironmentBlendModes;
	api->create_session = xrCreateSession;
	api->destroy_session = xrDestroySession;
	api->create_reference_space = xrCreateReferenceSpace;
	api->destroy_space = xrDestroySpace;
	api->locate_space = xrLocateSpace;
	api->enumerate_swapchain_formats = xrEnumerateSwapchainFormats;
	api->create_swapchain = xrCreateSwapchain;
	api->destroy_swapchain = xrDestroySwapchain;
	api->enumerate_swapchain_images = xrEnumerateSwapchainImages;
	api->acquire_swapchain_image = xrAcquireSwapchainImage;
	api->wait_swapchain_image = xrWaitSwapchainImage;
	api->release_swapchain_image = xrReleaseSwapchainImage;
	api->poll_event = xrPollEvent;
	api->begin_session = xrBeginSession;
	api->end_session = xrEndSession;
	api->request_exit_session = xrRequestExitSession;
	api->wait_frame = xrWaitFrame;
	api->begin_frame = xrBeginFrame;
	api->end_frame = xrEndFrame;
	api->locate_views = xrLocateViews;
	api->string_to_path = xrStringToPath;
	api->create_action_set = xrCreateActionSet;
	api->destroy_action_set = xrDestroyActionSet;
	api->create_action = xrCreateAction;
	api->suggest_interaction_profile_bindings = xrSuggestInteractionProfileBindings;
	api->attach_session_action_sets = xrAttachSessionActionSets;
	api->sync_actions = xrSyncActions;
	api->get_action_state_boolean = xrGetActionStateBoolean;
	api->get_action_state_float = xrGetActionStateFloat;
	api->get_action_state_vector2f = xrGetActionStateVector2f;
	api->apply_haptic_feedback = xrApplyHapticFeedback;
	api->stop_haptic_feedback = xrStopHapticFeedback;
}
