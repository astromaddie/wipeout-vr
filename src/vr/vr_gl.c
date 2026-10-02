#include <stdio.h>
#include <string.h>

#include "SDL.h"
#include "SDL_syswm.h"

#include "vr_gl.h"
#include "vr_log.h"

#define VR_GL_FRAMEBUFFER 0x8D40
#define VR_GL_READ_FRAMEBUFFER 0x8CA8
#define VR_GL_DRAW_FRAMEBUFFER 0x8CA9
#define VR_GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define VR_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define VR_GL_COLOR_ATTACHMENT0 0x8CE0
#define VR_GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define VR_GL_FRAMEBUFFER_SRGB 0x8DB9
#define VR_GL_SCISSOR_TEST 0x0C11
#define VR_GL_COLOR_CLEAR_VALUE 0x0C22
#define VR_GL_COLOR_BUFFER_BIT 0x4000
#define VR_GL_TEXTURE_2D 0x0DE1
#define VR_GL_LINEAR 0x2601
#define VR_GL_VENDOR 0x1F00
#define VR_GL_RENDERER 0x1F01
#define VR_GL_VERSION 0x1F02
#define VR_GL_MAJOR_VERSION 0x821B
#define VR_GL_MINOR_VERSION 0x821C

static struct {
	const uint8_t *(*GetString)(uint32_t name);
	void (*GetIntegerv)(uint32_t pname, int32_t *data);
	void (*GetFloatv)(uint32_t pname, float *data);
	uint8_t (*IsEnabled)(uint32_t cap);
	void (*Enable)(uint32_t cap);
	void (*Disable)(uint32_t cap);
	void (*ClearColor)(float r, float g, float b, float a);
	void (*Clear)(uint32_t mask);
	void (*GenFramebuffers)(int32_t n, uint32_t *ids);
	void (*DeleteFramebuffers)(int32_t n, const uint32_t *ids);
	void (*BindFramebuffer)(uint32_t target, uint32_t fbo);
	void (*FramebufferTexture2D)(uint32_t target, uint32_t attachment, uint32_t textarget, uint32_t texture, int32_t level);
	uint32_t (*CheckFramebufferStatus)(uint32_t target);
	void (*BlitFramebuffer)(int32_t sx0, int32_t sy0, int32_t sx1, int32_t sy1, int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1, uint32_t mask, uint32_t filter);
} gl;

static uint32_t draw_fbo = 0;

void vr_gl_version(int *major, int *minor) {
	int32_t ma = 0, mi = 0;
	gl.GetIntegerv(VR_GL_MAJOR_VERSION, &ma);
	gl.GetIntegerv(VR_GL_MINOR_VERSION, &mi);
	*major = ma;
	*minor = mi;
}


// -----------------------------------------------------------------------------
// The graphics binding

#if defined(__linux__)

static bool load_gl(void *out, const char *name) {
	void *fn = SDL_GL_GetProcAddress(name);
	if (!fn) {
		vr_logf("GL entry point %s is not available", name);
		return false;
	}
	memcpy(out, &fn, sizeof(fn));
	return true;
}

static bool load_all_gl(void) {
	return
		load_gl(&gl.GetString, "glGetString") &&
		load_gl(&gl.GetIntegerv, "glGetIntegerv") &&
		load_gl(&gl.GetFloatv, "glGetFloatv") &&
		load_gl(&gl.IsEnabled, "glIsEnabled") &&
		load_gl(&gl.Enable, "glEnable") &&
		load_gl(&gl.Disable, "glDisable") &&
		load_gl(&gl.ClearColor, "glClearColor") &&
		load_gl(&gl.Clear, "glClear") &&
		load_gl(&gl.GenFramebuffers, "glGenFramebuffers") &&
		load_gl(&gl.DeleteFramebuffers, "glDeleteFramebuffers") &&
		load_gl(&gl.BindFramebuffer, "glBindFramebuffer") &&
		load_gl(&gl.FramebufferTexture2D, "glFramebufferTexture2D") &&
		load_gl(&gl.CheckFramebufferStatus, "glCheckFramebufferStatus") &&
		load_gl(&gl.BlitFramebuffer, "glBlitFramebuffer");
}

// Before the OpenXR platform header; the X11 and GLX headers go first.
#include <X11/Xlib.h>
#include <GL/glx.h>
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static XrGraphicsBindingOpenGLXlibKHR binding;

bool vr_gl_init(void *sdl_window) {
	SDL_Window *window = sdl_window;
	if (!load_all_gl()) {
		return false;
	}

	const char *driver = SDL_GetCurrentVideoDriver();
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_X11) {
		vr_logf("the SDL window is not on x11 (video driver %s); XR_KHR_opengl_enable needs x11 and GLX", driver ? driver : "?");
		return false;
	}

	// SDL created the context with GLX, so its entry points come from SDL's GL
	// library. Going through SDL keeps libGL and libX11 out of our link line.
	Display *(*get_display)(void) = NULL;
	GLXContext (*get_context)(void) = NULL;
	GLXDrawable (*get_drawable)(void) = NULL;
	int (*query_context)(Display *, GLXContext, int, int *) = NULL;
	GLXFBConfig *(*choose_fbconfig)(Display *, int, const int *, int *) = NULL;
	if (!load_gl(&get_display, "glXGetCurrentDisplay") ||
		!load_gl(&get_context, "glXGetCurrentContext") ||
		!load_gl(&get_drawable, "glXGetCurrentDrawable") ||
		!load_gl(&query_context, "glXQueryContext") ||
		!load_gl(&choose_fbconfig, "glXChooseFBConfig")
	) {
		return false;
	}

	Display *display = get_display();
	GLXContext context = get_context();
	GLXDrawable drawable = get_drawable();
	if (!display || !context) {
		vr_logf("the current GL context is not GLX (video driver %s); the Xlib binding needs x11 without EGL", driver ? driver : "?");
		return false;
	}
	if (display != (Display *)wm.info.x11.display) {
		vr_logf("WARNING: GLX display %p is not SDL's X display %p", (void *)display, (void *)wm.info.x11.display);
	}

	GLXFBConfig fbconfig = NULL;
	int fbconfig_id = 0;
	query_context(display, context, GLX_FBCONFIG_ID, &fbconfig_id);
	const int attributes[] = { GLX_FBCONFIG_ID, fbconfig_id, None };
	int config_count = 0;
	GLXFBConfig *configs = choose_fbconfig(display, DefaultScreen(display), attributes, &config_count);
	if (configs && config_count > 0) {
		fbconfig = configs[0];
	}
	// `configs` is not XFree'd: one small allocation per run, and freeing it
	// would need libX11 on the link line.

	memset(&binding, 0, sizeof(binding));
	binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
	binding.xDisplay = display;
	binding.visualid = 0;
	binding.glxFBConfig = fbconfig;
	binding.glxDrawable = drawable;
	binding.glxContext = context;

	int major, minor;
	vr_gl_version(&major, &minor);
	vr_logf("graphics binding: opengl_xlib (SDL video driver %s), display %p, context %p, drawable 0x%lx",
		driver ? driver : "?", (void *)display, (void *)context, (unsigned long)drawable);
	vr_logf("gl: %s | %s | %s (context %d.%d)",
		(const char *)gl.GetString(VR_GL_VENDOR), (const char *)gl.GetString(VR_GL_RENDERER),
		(const char *)gl.GetString(VR_GL_VERSION), major, minor);

	gl.GenFramebuffers(1, &draw_fbo);
	return true;
}

const void *vr_gl_graphics_binding(void) {
	return &binding;
}

#else

bool vr_gl_init(void *sdl_window) {
	(void)sdl_window;
	vr_logf("the OpenGL/Xlib binding needs Linux and x11; VR is unavailable on this system");
	return false;
}

const void *vr_gl_graphics_binding(void) {
	return NULL;
}

#endif


void vr_gl_shutdown(void) {
	if (gl.DeleteFramebuffers && draw_fbo) {
		gl.DeleteFramebuffers(1, &draw_fbo);
	}
	draw_fbo = 0;
}


// -----------------------------------------------------------------------------
// Copies into swapchain images

typedef struct {
	int32_t read_fbo;
	int32_t draw_fbo;
	bool scissor;
	bool srgb;
	float clear_color[4];
} gl_state_t;

static void state_save(gl_state_t *s) {
	gl.GetIntegerv(VR_GL_READ_FRAMEBUFFER_BINDING, &s->read_fbo);
	gl.GetIntegerv(VR_GL_DRAW_FRAMEBUFFER_BINDING, &s->draw_fbo);
	gl.GetFloatv(VR_GL_COLOR_CLEAR_VALUE, s->clear_color);
	s->scissor = gl.IsEnabled(VR_GL_SCISSOR_TEST);
	s->srgb = gl.IsEnabled(VR_GL_FRAMEBUFFER_SRGB);
	if (s->scissor) {
		gl.Disable(VR_GL_SCISSOR_TEST);
	}
	if (s->srgb) {
		gl.Disable(VR_GL_FRAMEBUFFER_SRGB);
	}
}

static void state_restore(const gl_state_t *s) {
	gl.BindFramebuffer(VR_GL_READ_FRAMEBUFFER, (uint32_t)s->read_fbo);
	gl.BindFramebuffer(VR_GL_DRAW_FRAMEBUFFER, (uint32_t)s->draw_fbo);
	gl.ClearColor(s->clear_color[0], s->clear_color[1], s->clear_color[2], s->clear_color[3]);
	if (s->scissor) {
		gl.Enable(VR_GL_SCISSOR_TEST);
	}
	if (s->srgb) {
		gl.Enable(VR_GL_FRAMEBUFFER_SRGB);
	}
}

void vr_gl_clear_image(uint32_t image, uint32_t width, uint32_t height, float r, float g, float b, float a) {
	(void)width; (void)height;
	gl_state_t saved;
	state_save(&saved);

	gl.BindFramebuffer(VR_GL_DRAW_FRAMEBUFFER, draw_fbo);
	gl.FramebufferTexture2D(VR_GL_DRAW_FRAMEBUFFER, VR_GL_COLOR_ATTACHMENT0, VR_GL_TEXTURE_2D, image, 0);
	if (gl.CheckFramebufferStatus(VR_GL_DRAW_FRAMEBUFFER) == VR_GL_FRAMEBUFFER_COMPLETE) {
		gl.ClearColor(r, g, b, a);
		gl.Clear(VR_GL_COLOR_BUFFER_BIT);
	}
	else {
		vr_logf("the swapchain image %u is not a complete framebuffer", image);
	}
	gl.FramebufferTexture2D(VR_GL_DRAW_FRAMEBUFFER, VR_GL_COLOR_ATTACHMENT0, VR_GL_TEXTURE_2D, 0, 0);

	state_restore(&saved);
}

void vr_gl_copy_backbuffer(uint32_t image, uint32_t width, uint32_t height, uint32_t src_width, uint32_t src_height) {
	gl_state_t saved;
	state_save(&saved);

	gl.BindFramebuffer(VR_GL_READ_FRAMEBUFFER, 0);
	gl.BindFramebuffer(VR_GL_DRAW_FRAMEBUFFER, draw_fbo);
	gl.FramebufferTexture2D(VR_GL_DRAW_FRAMEBUFFER, VR_GL_COLOR_ATTACHMENT0, VR_GL_TEXTURE_2D, image, 0);
	if (gl.CheckFramebufferStatus(VR_GL_DRAW_FRAMEBUFFER) == VR_GL_FRAMEBUFFER_COMPLETE) {
		gl.BlitFramebuffer(
			0, 0, (int32_t)src_width, (int32_t)src_height,
			0, 0, (int32_t)width, (int32_t)height,
			VR_GL_COLOR_BUFFER_BIT, VR_GL_LINEAR
		);
	}
	else {
		vr_logf("the swapchain image %u is not a complete framebuffer", image);
	}
	gl.FramebufferTexture2D(VR_GL_DRAW_FRAMEBUFFER, VR_GL_COLOR_ATTACHMENT0, VR_GL_TEXTURE_2D, 0, 0);

	state_restore(&saved);
}
