#ifndef VR_GL_H
#define VR_GL_H

#include <stdbool.h>
#include <stdint.h>

// The GL side of the XR host. The graphics binding for XR_KHR_opengl_enable
// comes from the GLX context SDL created for its x11 window (Linux only; on
// other systems vr_gl_init() fails). GL entry points are loaded through SDL, so
// this file needs no GL headers and does not depend on the game's GL loader.

// `sdl_window` is an SDL_Window *, with its GL context current. False, after
// logging why, when the window is not x11/GLX or an entry point is missing.
bool vr_gl_init(void *sdl_window);
void vr_gl_shutdown(void);

// An XrGraphicsBindingOpenGLXlibKHR *, valid after vr_gl_init().
const void *vr_gl_graphics_binding(void);

// Version of the current context, from GL_MAJOR_VERSION and GL_MINOR_VERSION.
void vr_gl_version(int *major, int *minor);

// Fill a swapchain image (a GL texture name) with one colour. The values land
// in the image as they are: GL_FRAMEBUFFER_SRGB is off for the clear.
void vr_gl_clear_image(uint32_t image, uint32_t width, uint32_t height, float r, float g, float b, float a);

// Copy the window's back buffer (src_width x src_height, bottom-up like the
// image) into the image, scaling linearly when the sizes differ. Values are
// copied unconverted (GL_FRAMEBUFFER_SRGB off), as the frame is already
// gamma-encoded. Call before the buffer swap.
void vr_gl_copy_backbuffer(uint32_t image, uint32_t width, uint32_t height, uint32_t src_width, uint32_t src_height);

#endif
