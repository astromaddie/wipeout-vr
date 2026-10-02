#include <stdio.h>
#include <string.h>

#include "SDL.h"

#include "vr.h"
#include "vr_compose.h"
#include "vr_gl.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_math.h"
#include "vr_paths.h"
#include "vr_perf.h"
#include "vr_render.h"
#include "vr_xr.h"

#include "platform.h"

#define VR_APP_NAME "WipEoutVR"
#define VR_PROBE_SESSION_WAIT_MS 20000
#define VR_SHUTDOWN_WAIT_MS 1000


static struct {
	bool enabled;
	vr_settings_t settings;
	vr_render_settings_t render_settings;
	vr_xr_t xr;
	char dump_dir[1100];
	vr_input_t *input; // the action set; NULL when it could not be made

	// Surfaces besides the eyes: the HUD and the mono panel (ui size) and the fade (tiny)
	vr_swapchain_t hud;
	vr_swapchain_t panel;
	vr_swapchain_t fade;
	uint32_t recentres_seen;
	uint32_t focus_gains_seen;

	// What this frame submits: from the renderer's replay (layers), or from the probe
	vr_layers_t layers;
	bool layers_valid;
	bool want_projection;
	vr_compose_t compose;
	XrCompositionLayerProjection probe_layer;
	XrCompositionLayerProjectionView probe_views[2];

	vr_perf_t perf;
} vr;


static float setting(const char *name) {
	return (float)vr_settings_get(&vr.settings, name);
}

bool vr_enabled(void) {
	return vr.enabled;
}

bool vr_exit_requested(void) {
	return vr.enabled && vr.xr.exit_requested;
}

const vr_settings_t *vr_settings(void) {
	return &vr.settings;
}

const vr_frame_t *vr_frame(void) {
	return &vr.xr.frame;
}

double vr_frame_dt(void) {
	return vr.xr.frame_dt;
}

bool vr_sim_frozen(void) {
	return vr.enabled && !vr_xr_sim_active(&vr.xr);
}


// -----------------------------------------------------------------------------
// The renderer's provider: the runtime's frame and swapchains

static bool frame_renders(void) {
	return vr.xr.frame_open && (vr.xr.frame.should_render || vr.xr.config.force_render);
}

static const vr_frame_t *provider_frame(void *user, bool *renders) {
	(void)user;
	*renders = frame_renders();
	return &vr.xr.frame;
}

// The HUD and the mono panel are the 2D layout's size; a swapchain of its own each
static vr_swapchain_t *ui_swapchain(vr_surface_t surface) {
	vr_swapchain_t *sc = surface == VR_SURFACE_HUD ? &vr.hud : &vr.panel;
	if (sc->handle == XR_NULL_HANDLE) {
		if (!vr_xr_swapchain_create(&vr.xr, sc, (uint32_t)vr.render_settings.ui_width, (uint32_t)vr.render_settings.ui_height)) {
			return NULL;
		}
	}
	return sc;
}

static bool provider_acquire(void *user, vr_surface_t surface, vr_target_t *out) {
	(void)user;
	vr_swapchain_t *sc;
	if (surface == VR_SURFACE_EYE0 || surface == VR_SURFACE_EYE1) {
		sc = &vr.xr.eye[surface - VR_SURFACE_EYE0];
	}
	else {
		sc = ui_swapchain(surface);
	}
	uint32_t index;
	if (!sc || sc->handle == XR_NULL_HANDLE || !vr_xr_swapchain_acquire(&vr.xr, sc, &index)) {
		return false;
	}
	out->image = sc->images[index].image;
	out->width = sc->width;
	out->height = sc->height;
	return true;
}

static void provider_release(void *user, vr_surface_t surface) {
	(void)user;
	vr_swapchain_t *sc = surface == VR_SURFACE_HUD ? &vr.hud
		: surface == VR_SURFACE_PANEL ? &vr.panel
		: &vr.xr.eye[surface - VR_SURFACE_EYE0];
	vr_xr_swapchain_release(&vr.xr, sc);
}

static void provider_submit(void *user, const vr_layers_t *layers) {
	(void)user;
	vr.layers = *layers;
	vr.layers_valid = true;
}


// -----------------------------------------------------------------------------
// Init and shutdown

bool vr_init(void *sdl_window) {
	memset(&vr, 0, sizeof(vr));

	const char *dir = vr_paths_runtime_data_dir();
	if (dir[0]) {
		char path[1100];
		snprintf(path, sizeof(path), "%svr.toml", dir);
		vr_settings_load(&vr.settings, path);
		vr_logf("data dir %s", dir);
	}
	else {
		vr_logf("no data dir (XDG_DATA_HOME and HOME are unset); using default settings");
		vr_settings_defaults(&vr.settings);
		vr_settings_apply_env(&vr.settings, NULL);
	}

	if (!vr_gl_init(sdl_window)) {
		vr_logf("VR unavailable: no usable GL binding; running flat");
		return false;
	}

	vr_xr_api_t api;
	vr_xr_api_loader(&api);
	vr_xr_config_t config = {
		.app_name = VR_APP_NAME,
		.resolution_scale = setting("resolution_scale"),
		.refresh_rate = (int)setting("refresh_rate"),
		.force_render = setting("force_render") != 0
	};
	if (!vr_xr_instance_create(&vr.xr, &api, &config)) {
		vr_logf("VR unavailable: no OpenXR runtime or headset; running flat");
		vr_gl_shutdown();
		return false;
	}

	int major, minor;
	vr_gl_version(&major, &minor);
	uint32_t min_major = XR_VERSION_MAJOR(vr.xr.gl_min_version);
	uint32_t min_minor = XR_VERSION_MINOR(vr.xr.gl_min_version);
	if ((uint32_t)major < min_major || ((uint32_t)major == min_major && (uint32_t)minor < min_minor)) {
		vr_logf("VR unavailable: the runtime needs GL %u.%u, the context is %d.%d; running flat", min_major, min_minor, major, minor);
		vr_xr_destroy(&vr.xr);
		vr_gl_shutdown();
		return false;
	}

	if (!vr_xr_session_create(&vr.xr, vr_gl_graphics_binding())) {
		vr_logf("VR unavailable: the session could not be created; running flat");
		vr_xr_destroy(&vr.xr);
		vr_gl_shutdown();
		return false;
	}

	vr_logf("session created: swapchain format 0x%llx (%s), eye %ux%u, resolution_scale %.2f",
		(long long)vr.xr.color_format, vr.xr.color_srgb ? "sRGB" : "linear RGBA8",
		vr.xr.eye[0].width, vr.xr.eye[0].height, config.resolution_scale);
	if (vr.xr.color_format != VR_GL_SRGB8_ALPHA8 && vr.xr.color_format != VR_GL_RGBA8) {
		vr_logf("WARNING: the swapchain format has no alpha channel, so the HUD quad cannot be translucent");
	}
	vr.input = vr_input_create(&vr.xr);
	vr.enabled = true;

	// The stereo renderer records the game's draw calls and replays them (vr_render.h).
	// It must be attached before render_init().
	vr_render_settings_from(&vr.render_settings, &vr.settings);
	vr_render_config_t render_config = {
		.provider = {
			.user = NULL,
			.frame = provider_frame,
			.acquire = provider_acquire,
			.release = provider_release,
			.submit = provider_submit
		},
		.settings = vr.render_settings
	};
	if (dir[0]) {
		snprintf(vr.dump_dir, sizeof(vr.dump_dir), "%svr-dumps", dir);
		render_config.dump_dir = vr.dump_dir;
	}
	vr_render_attach(&render_config);
	vr_perf_init(&vr.perf);
	return true;
}

void vr_shutdown(void) {
	if (!vr.enabled) {
		return;
	}
	vr.enabled = false;

	// A clean exit: ask, then keep polling until the runtime has stopped the session
	vr_xr_request_exit(&vr.xr);
	uint32_t waited = 0;
	while (vr.xr.session_running && waited < VR_SHUTDOWN_WAIT_MS) {
		vr_xr_poll_events(&vr.xr);
		SDL_Delay(5);
		waited += 5;
	}

	vr_input_destroy(vr.input, &vr.xr);
	vr.input = NULL;
	vr_render_detach();
	vr_xr_swapchain_destroy(&vr.xr, &vr.hud);
	vr_xr_swapchain_destroy(&vr.xr, &vr.panel);
	vr_xr_swapchain_destroy(&vr.xr, &vr.fade);
	vr_xr_destroy(&vr.xr);
	vr_gl_shutdown();
	vr_settings_free(&vr.settings);
}


// -----------------------------------------------------------------------------
// Frame loop

bool vr_frame_begin(void) {
	vr_xr_poll_events(&vr.xr);

	// A system recentre (the runtime moved the LOCAL origin), the app getting focus
	// again (maybe another wearer) or the input layer's request: the renderer
	// recentres at the next frame boundary.
	bool recentre = vr_take_recentre_request();
	if (vr.recentres_seen != vr.xr.recentre_requests || vr.focus_gains_seen != vr.xr.focus_gains) {
		vr.recentres_seen = vr.xr.recentre_requests;
		vr.focus_gains_seen = vr.xr.focus_gains;
		recentre = true;
	}
	if (recentre) {
		vr_render_request_recentre();
	}

	vr.layers_valid = false;
	vr.want_projection = false;
	bool begun = vr_xr_frame_begin(&vr.xr);
	// Actions after xrBeginFrame; with no frame, no focus or no session this releases every input
	vr_input_update(vr.input, &vr.xr, setting("haptics"));
	return begun;
}

// Clears the fade swapchain's image to black at `alpha`: the quad the compositor blends over everything
static bool fade_render(float alpha) {
	if (vr.fade.handle == XR_NULL_HANDLE && !vr_xr_swapchain_create(&vr.xr, &vr.fade, 4, 4)) {
		return false;
	}
	uint32_t index;
	if (!vr_xr_swapchain_acquire(&vr.xr, &vr.fade, &index)) {
		return false;
	}
	vr_gl_clear_image(vr.fade.images[index].image, vr.fade.width, vr.fade.height, 0, 0, 0, alpha);
	vr_xr_swapchain_release(&vr.xr, &vr.fade);
	return true;
}

void vr_frame_end(void) {
	const XrCompositionLayerBaseHeader *probe[1];
	const XrCompositionLayerBaseHeader *const *layers = NULL;
	uint32_t count = 0;

	if (vr.layers_valid) {
		bool fade = vr.layers.fade_alpha > VR_FADE_MIN_ALPHA && fade_render(vr.layers.fade_alpha);
		count = vr_compose_layers(&vr.compose, &vr.xr, &vr.layers, &vr.hud, &vr.panel, fade ? &vr.fade : NULL);
		layers = vr.compose.list;
	}
	else if (vr.want_projection) {
		// The probe's eyes
		vr_xr_projection_layer(&vr.xr, &vr.probe_layer, vr.probe_views);
		probe[0] = (const XrCompositionLayerBaseHeader *)&vr.probe_layer;
		layers = probe;
		count = 1;
	}
	vr_xr_frame_end(&vr.xr, layers, count);

	if (vr.render_settings.timing) {
		const vr_render_timing_t *t = vr_render_timing();
		char line[256];
		double now_s = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
		if (vr_perf_frame(&vr.perf, now_s, vr.xr.frame.predicted_display_time, vr.xr.frame.predicted_display_period,
			t->record_ms, t->replay_ms, t->hud_ms, t->eye_ms, line, sizeof(line))
		) {
			printf("%s\n", line);
			fflush(stdout);
		}
	}
}

void vr_present_frame(void) {
	vr_frame_end();
}

bool vr_eye_acquire(int eye, uint32_t *gl_image, uint32_t *width, uint32_t *height) {
	uint32_t index;
	if (!vr.enabled || eye < 0 || eye > 1 || !vr_xr_swapchain_acquire(&vr.xr, &vr.xr.eye[eye], &index)) {
		return false;
	}
	*gl_image = vr.xr.eye[eye].images[index].image;
	*width = vr.xr.eye[eye].width;
	*height = vr.xr.eye[eye].height;
	return true;
}

void vr_eye_release(int eye) {
	vr_xr_swapchain_release(&vr.xr, &vr.xr.eye[eye]);
}

void vr_submit_projection(void) {
	if (frame_renders() && vr.xr.frame.views_valid) {
		vr.want_projection = true;
	}
}


// -----------------------------------------------------------------------------
// Probe: left eye red, right eye blue, before any game code

void vr_probe(void) {
	float seconds = setting("probe_seconds");
	if (!vr.enabled || seconds <= 0) {
		return;
	}

	vr_logf("probe: %.1f s, left eye red, right eye blue", seconds);
	vr_logf("probe: swapchain format 0x%llx (%s), eye size %ux%u (recommended %ux%u), graphics binding opengl_xlib",
		(long long)vr.xr.color_format, vr.xr.color_srgb ? "sRGB" : "linear RGBA8",
		vr.xr.eye[0].width, vr.xr.eye[0].height, vr.xr.eye_recommended[0][0], vr.xr.eye_recommended[0][1]);

	uint32_t start = SDL_GetTicks();
	uint32_t running_since = 0;
	bool running = false;
	bool standby_logged = false;
	uint32_t frames = 0;
	uint32_t rendered = 0;

	for (;;) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				platform_exit();
				return;
			}
		}
		uint32_t now = SDL_GetTicks();

		if (!vr_frame_begin()) {
			if (vr.xr.exit_requested) {
				break;
			}
			if (!running && now - start > VR_PROBE_SESSION_WAIT_MS) {
				vr_logf("probe: the session did not start within %d s", VR_PROBE_SESSION_WAIT_MS / 1000);
				break;
			}
			SDL_Delay(5);
			continue;
		}

		if (!running) {
			running = true;
			running_since = now;
			vr_logf("probe: session running");
		}
		bool done = (now - running_since) >= (uint32_t)(seconds * 1000.0f);
		frames++;

		if (!done && frame_renders() && vr.xr.frame.views_valid) {
			for (int eye = 0; eye < 2; eye++) {
				uint32_t image, w, h;
				if (vr_eye_acquire(eye, &image, &w, &h)) {
					vr_gl_clear_image(image, w, h, eye == 0 ? 1.0f : 0.0f, 0.0f, eye == 0 ? 0.0f : 1.0f, 1.0f);
					vr_eye_release(eye);
				}
			}
			vr_submit_projection();
			rendered++;
		}
		else if (!done && !vr.xr.frame.should_render && !standby_logged) {
			standby_logged = true;
			vr_logf("probe: standby (shouldRender is false); the loop keeps running, nothing is drawn");
		}

		vr_frame_end();
		if (done) {
			break;
		}
	}

	vr_logf("probe: done, %u frames, %u with both eyes drawn", frames, rendered);
}
