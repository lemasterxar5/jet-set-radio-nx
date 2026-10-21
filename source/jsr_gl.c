/* jsr_gl.c -- the EGL side of the game's JetsetradioView.
 *
 * libjetsetradio.so imports no egl* at all: SEGA's Android build let the
 * engine's GLSurfaceView (JetsetradioView + its Renderer) own EGL, and the
 * engine only ever draws with libGLESv2. Its CVideoAndroid builds the
 * shaders, the buffers and the textures itself, straight on the context Java
 * made.
 *
 * This file is that Java: one window surface on the default NWindow with a
 * GLES 2 context, and the swap. It calls the runtime's own EGL shims
 * (gl_mesa.c's b_egl*), so the frame hooks, the CPU boost and the frame
 * count see every frame. MIT.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <switch.h>

#include "gl_layer.h"
#include "rt_cfg.h"
#include "rt_settings.h"
#include "rt_window.h"
#include "jsr.h"
#include "jsr_fps.h"
#ifdef JSR_TARGET_PROBE
#include "jsr_target.h"
#endif
#include "util.h"
#ifndef JSR_DEPTH_BITS
#define JSR_DEPTH_BITS 24
#endif
#ifdef JSR_DEPTH_PROBE
void jsr_depth_probe(void);
#endif

/* gl_mesa.c / gl_null.c */
EGLDisplay b_eglGetDisplay(EGLNativeDisplayType d);
EGLBoolean b_eglInitialize(EGLDisplay d, EGLint *maj, EGLint *min);
EGLBoolean b_eglChooseConfig(EGLDisplay d, const EGLint *attrs, EGLConfig *cfgs, EGLint cap, EGLint *num);
EGLContext b_eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrs);
EGLSurface b_eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *attrs);
EGLBoolean b_eglMakeCurrent(EGLDisplay d, EGLSurface dr, EGLSurface rd, EGLContext c);
EGLBoolean b_eglSwapBuffers(EGLDisplay d, EGLSurface s);
EGLBoolean b_eglSwapInterval(EGLDisplay d, EGLint i);
EGLBoolean b_eglQuerySurface(EGLDisplay d, EGLSurface s, EGLint a, EGLint *v);
EGLint b_eglGetError(void);

static Mutex g_lock;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx;
static int g_ready;
static int g_fps_overlay;

/* Display, config, surface and the main context; g_lock held.
 *
 * The engine's CVideoAndroid asks for what it wants through njInitSystem /
 * its own globals, not through a Java callback, so the config is the one the
 * game needs everywhere: RGBA8, a 24-bit depth buffer (Ninja's z-sorting
 * depends on it) and an 8-bit stencil (the graffiti canvas and the mission
 * map mask). */
static int make_main(void) {
  g_dpy = b_eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint maj = 0, min = 0, n = 0;
  if (!b_eglInitialize(g_dpy, &maj, &min)) {
    debugPrintf("[gl] eglInitialize failed 0x%x\n", b_eglGetError());
    return 0;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                    EGL_DEPTH_SIZE, JSR_DEPTH_BITS, EGL_STENCIL_SIZE, 8, EGL_NONE};
  if (!b_eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &n) || n < 1) {
    debugPrintf("[gl] no EGL config (0x%x)\n", b_eglGetError());
    return 0;
  }
  g_surf = b_eglCreateWindowSurface(g_dpy, g_cfg, 0, NULL);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  g_ctx = b_eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g_surf == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT) {
    debugPrintf("[gl] surface %p / context %p not made (0x%x)\n", g_surf, g_ctx, b_eglGetError());
    return 0;
  }
  /* The screen refreshes at 60 Hz: interval 1 = 60 fps, 2 = 30 fps
   * ([display] frame_rate; 30 runs cooler in handheld). */
  b_eglSwapInterval(g_dpy, rt_config_int("display", "frame_rate") == 1 ? 2 : 1);
  g_ready = 1;
  g_fps_overlay = rt_config_value("display.fps_overlay", 0);
  EGLint actual_depth = 0, actual_stencil = 0;
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_DEPTH_SIZE, &actual_depth);
  eglGetConfigAttrib(g_dpy, g_cfg, EGL_STENCIL_SIZE, &actual_stencil);
  debugPrintf("[gl] EGL %d.%d: window surface, GLES 2 context (RGBA8, depth %d, stencil %d)\n",
              (int)maj, (int)min, (int)actual_depth, (int)actual_stencil);
  return 1;
}

int jsr_egl_ensure(void) {
  mutexLock(&g_lock);
  int ok = g_ready || make_main();
  if (ok && eglGetCurrentContext() != g_ctx)
    ok = b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) == EGL_TRUE;
#ifdef JSR_TARGET_PROBE
  if (ok) {
    EGLint width = 0, height = 0;
    b_eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &width);
    b_eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &height);
    jsr_target_init(width, height);
  }
#endif
#ifdef JSR_DEPTH_PROBE
  if (ok)
    jsr_depth_probe();
#endif
  mutexUnlock(&g_lock);
  return ok;
}

void jsr_egl_swap(void) {
  if (g_ready && eglGetCurrentContext() == g_ctx) {
#ifdef JSR_TARGET_PROBE
    jsr_target_present();
#endif
    if (g_fps_overlay) {
      EGLint width = 0, height = 0;
      b_eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &width);
      b_eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &height);
      jsr_fps_draw(width, height);
    }
    if (b_eglSwapBuffers(g_dpy, g_surf) && g_fps_overlay)
      jsr_fps_presented(armTicksToNs(armGetSystemTick()));
  }
}
