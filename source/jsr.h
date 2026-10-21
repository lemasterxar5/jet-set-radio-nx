/* jsr.h -- what the port's files share. MIT. */
#ifndef JSR_H
#define JSR_H

#include <stddef.h>
#include <stdint.h>

#include "dcr_setup.h"
#include "jni.h"
#include "so_util.h"

/* The one Java class the engine talks to. libjetsetradio.so calls nothing of
 * the Android framework: initIDs() looks every method up on its own class (on
 * GetObjectClass(thiz), not with FindClass), and the engine reads its data
 * through fopen/open like any other C program. The port supplies that class,
 * its methods (jsr_java.c) and the natives the engine exports. */
#define JSR_CLS_LIB "com/sega/jetsetradio/JetsetradioLib"

/* --------------------------------------------------------------- jsr_obb.c */
/* The game's data (the AFS archives under DATA, and the game's own UI under
 * CUSTOM): the expansion file in <game folder>/obb, unpacked once into
 * <game folder>/data/files. Its Android path, as the game will see it
 * through getBasePath(). */
const char *jsr_data_base(void);   /* "/data/data/<pkg>/files/" */
int jsr_data_ready(void);          /* 1 once the data folder is there */
/* Unpacks the expansion file (the setup's bar). 1 done, 0 nothing to do,
 * -1 no expansion file and no extracted data. */
int jsr_data_unpack(RtSetupCtx *ctx);
void jsr_data_stage(RtSetupCtx *ctx); /* the RtSetupStep's run() */
/* Why it gave up, one line, in words the player can act on (the screen only
 * ever shows this; debug.log has the detail). */
const char *jsr_data_problem(void);
/* The expansion file in <game folder>/obb (any *.obb): its SD path in `out`.
 * patch is accepted for the shape the other ports' scan has; this game ships
 * one expansion file. */
int jsr_obb_find(int patch, char *out, size_t cap);

/* --------------------------------------------------------------- jsr_gl.c */
/* The EGL display, config, window surface and context the game's
 * JetsetradioView (a GLSurfaceView) would have made. Idempotent. 1 when the
 * main context is current on the calling thread. */
int jsr_egl_ensure(void);
/* eglSwapBuffers on the window surface (through the runtime's frame hooks). */
void jsr_egl_swap(void);

/* ------------------------------------------------------------ jsr_audio.c */
/* The sound the engine mixes itself: audioFillStreamBuffer() pulls the next
 * block out of the game, so one pump thread (rt_audout_pump_start) is the
 * whole of it. Started once the engine is up, stopped with the game. */
int jsr_audio_start(void);
void jsr_audio_bind(so_module *module);
void jsr_audio_stop(void);
void jsr_audio_pause(int paused);

/* ------------------------------------------------------------ jsr_input.c */
/* Called once per frame, before step(): the touch screen as the touches the
 * engine asked for, and the buttons as their layout points. */
void jsr_input_init(void);
void jsr_input_poll(void);
int jsr_input_pad_connected(void);
/* One press at a point, as its own batch (the frame loop's KEY_PLUS). */
void jsr_input_tap(int x, int y);

/* ------------------------------------------------------------ jsr_java.c */
/* The engine's entry points (libjetsetradio.so's Java_com_sega_jetsetradio_*),
 * resolved by jsr_main.c. Static natives: (env, class, args...). */
typedef struct JsrNative {
  void (*init)(void *env, void *cls, int width, int height, int language);
  void (*step)(void *env, void *cls);
  void (*exit)(void *env, void *cls);
  void (*onPause)(void *env, void *cls);
  void (*onResume)(void *env, void *cls);
  void (*backPressed)(void *env, void *cls);
  void (*startSendingTouches)(void *env, void *cls);
  void (*sendTouch)(void *env, void *cls, int x, int y, float nx, float ny, float down, float aux);
  void (*endSendingTouches)(void *env, void *cls);
  void (*finalTextureRender)(void *env, void *cls, int which);
  jint (*audioFillStreamBuffer)(void *env, void *cls, jint stream, void *buf, jint samples);
  jint (*isSaving)(void *env, void *cls);
  jint (*isLocked30Fps)(void *env, void *cls);
  jint (*getGraffitiWidth)(void *env, void *cls);
  jint (*getGraffitiHeight)(void *env, void *cls);
  void *(*getGraffitiImage)(void *env, void *cls, int w, int h, void *bb);
  void *(*getSubtitle)(void *env, void *cls, int which);
  void (*updateInput)(void *env, void *cls, int changed, int left, int right, int up, int down,
                      int jump, int back, int graffiti, int dash, int pause,
                      int left_x, int left_y, int right_x, int right_y);
  void (*setHardwareLevel)(void *env, void *cls, int level);
  jint (*licenseID)(void *env, void *cls);
} JsrNative;
extern JsrNative g_nat;
extern void *g_lib_cls;   /* the JetsetradioLib class object, as natives take it */
extern JObj *g_lib_obj;   /* its instance: the receiver every native gets */
extern int g_jsr_inited;  /* 1 once init() has created the engine */

/* After jni_init(): MonitorEnter/Exit lock for real (the runtime's are no-ops;
 * the engine's loader threads would read each other's shared buffers). */
void jsr_java_patch_monitors(void);

#endif /* JSR_H */
