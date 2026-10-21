/* jsr_main.c -- Jet Set Radio: from the APK to the frame loop.
 *
 * port_load: the setup unpacks libjetsetradio.so and the class list from the
 * APK (the first start, and whenever the APK changes) and the 598 MB of game
 * data out of the expansion file; then the library is loaded, relocated, bound
 * to the runtime's shims and sealed as code.
 *
 * port_run: the library's constructors and JNI_OnLoad, the GLES 2 context the
 * game's JetsetradioView would have made, init(), and then what Java's
 * GLSurfaceView.Renderer did: every frame onDrawFrame() called step() --
 * which is the engine's whole frame, loopStep(): input, action, draw -- and
 * then the swap, which is GLSurfaceView's. The game has no EGL of its own:
 * jsr_gl.c is its JetsetradioView.
 *
 * One thing is the mirror image of every other Android port: the sound is
 * pulled out of the engine (jsr_audio.c) instead of pushed into a Java
 * AudioTrack, because this engine mixes its own and only ever asks Java to
 * fill the next block. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

#include "bionic_io.h"
#include "dcr_boost.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "gl_layer.h"
#include "jsr.h"
#include "jsr_timing.h"
#include "jsr_native60.h"
#include "jsr_memory.h"
#include "jsr_render_safety.h"
#include "jsr_visuals.h"
#include "jsr_fps.h"
#include "rt_applet.h"
#include "rt_audout.h"
#include "rt_boot.h"
#include "rt_cfg.h"
#include "rt_settings.h"
#include "rt_window.h"
#include "so_util.h"
#include "util.h"
#include "watchdog.h"

static so_module g_mod;
static volatile int g_running;

/* The game's library, and after it the 598 MB of data (850-999 of the bar). */
static const char *const k_libs[] = {"libjetsetradio.so"};
static const RtSetupStep k_steps[] = {
    {"the game's data", RT_STEP_AFTER_ZIP, 850, 999, jsr_data_stage},
};
const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game's library",
    .libs_p0 = 50,
    .libs_p1 = 800,
    .classes_p0 = 800,
    .classes_p1 = 850,
    .steps = k_steps,
    .nsteps = 1,
};

uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

/* [jsr] library: which of the APK's two builds to load. Both export the same
 * natives and need the same imports; libjetsetradio-nn.so is the larger one
 * (the "nn" build), and a player who finds one better may name it here. */
static void library_name(char *out, size_t cap) {
  const char *want = rt_config_get("jsr", "library");
  snprintf(out, cap, "%s", (want && *want) ? want : "libjetsetradio.so");
}

int port_load(const char *apk) {
  /* crash.log is appended to, one block per fault, and it was never cleared
   * between runs: three starts' worth of reports piled up in one file, and
   * they are all identical from one run to the next (same addresses, same
   * registers), so reading it tells you nothing about which faults this start
   * actually had. Each run starts with an empty one. */
  {
    char prev[DCR_PATH_MAX];
    rt_root_path(prev, sizeof prev, "crash.log");
    unlink(prev);
  }

  dcr_setup_from_apk(apk); /* the library, the class list, then the data */

  if (!jsr_data_ready()) {
    char obbdir[DCR_PATH_MAX];
    rt_root_path(obbdir, sizeof obbdir, "obb");
    char files[DCR_PATH_MAX];
    rt_root_path(files, sizeof files, "data/files");
    /* The reason first: that is the one line the player can act on (it was in
     * the log only, which is no use to somebody who cannot open a file). */
    fatal_error("The game has no data yet.\n\n%s\n\n"
                "The expansion file (.obb) goes in\n  %s\n"
                "and the console unpacks it into %s the first time it\n"
                "runs. There is nothing to do on a PC.\n\n"
                "debug.log has the details.",
                jsr_data_problem(), obbdir, files);
  }

  char lib[64], path[DCR_PATH_MAX];
  library_name(lib, sizeof lib);
  rt_root_path(path, sizeof path, lib);
  if (!jsr_memory_prepare())
    fatal_error("The game's fixed memory window is unavailable.\nRestart the application and try again.\nSee debug.log for details.");
  int rc = so_load(&g_mod, path, NULL, PORT_SO_REGION_BYTES);
  if (rc)
    fatal_error("%s could not be loaded (%d).\n\n%s", lib, rc, port_apk_help());
  if (so_relocate(&g_mod))
    fatal_error("%s: relocation failed.", lib);
  int left = so_resolve(&g_mod, NULL, 0, 0);
  if (left)
    debugPrintf("[boot] WARNING: %d import(s) of %s unresolved\n", left, lib);
  so_fix_kuser_helpers(&g_mod);
  jsr_timing_install(&g_mod);
  if (!jsr_visuals_install(&g_mod))
    debugPrintf("[visuals] distance profile not applied; original visibility retained\n");
  if (!jsr_render_safety_install(&g_mod))
    fatal_error("The game library does not match the supported shadow renderer profile.");
  so_finalize(&g_mod);
  so_flush_caches(&g_mod);
  jsr_audio_bind(&g_mod);
  debugPrintf("[boot] %s loaded\n", lib);
  return 0;
}

/* The engine's entry points. init() takes three ints the vendor's Java passed
 * (the ones it reads into the globals its init needs); step(), exit(),
 * onPause(), onResume(), backPressed() and the touch batch take none. */
#define NAT(field, sym, required)                                                          \
  do {                                                                                     \
    g_nat.field = (void *)so_try_find_addr_rx(&g_mod, "Java_com_sega_jetsetradio_JetsetradioLib_" sym); \
    if (!(required) || g_nat.field)                                                        \
      debugPrintf("[boot] %s: %s\n", sym, g_nat.field ? "found" : "MISSING");              \
    if ((required) && !g_nat.field)                                                        \
      fatal_error("The game's library has no " sym);                                       \
  } while (0)

static void find_natives(void) {
  NAT(init, "init", 1);
  NAT(step, "step", 1);
  NAT(exit, "exit", 0);
  NAT(onPause, "onPause", 0);
  NAT(onResume, "onResume", 0);
  NAT(backPressed, "backPressed", 0);
  NAT(startSendingTouches, "startSendingTouches", 1);
  NAT(sendTouch, "sendTouch", 1);
  NAT(endSendingTouches, "endSendingTouches", 1);
  NAT(finalTextureRender, "finalTextureRender", 0);
  NAT(audioFillStreamBuffer, "audioFillStreamBuffer", 1);
  NAT(isSaving, "isSaving", 0);
  NAT(isLocked30Fps, "isLocked30Fps", 0);
  NAT(getGraffitiWidth, "getGraffitiWidth", 0);
  NAT(getGraffitiHeight, "getGraffitiHeight", 0);
  NAT(getGraffitiImage, "getGraffitiImage", 0);
  NAT(getSubtitle, "getSubtitle", 0);
  NAT(updateInput, "updateInput", 0);
  NAT(setHardwareLevel, "setHardwareLevel", 0);
  NAT(licenseID, "licenseID", 0);
}

/* ------------------------------------------------------------------ focus */

void port_focus_lost(void) {
  if (!g_running)
    return;
  jsr_audio_pause(1);
  if (g_nat.onPause)
    g_nat.onPause(g_jni_env, g_lib_cls);
}

void port_focus_gained(void) {
  jsr_timing_reset();
  jsr_fps_reset();
  if (!g_running)
    return;
  if (g_nat.onResume)
    g_nat.onResume(g_jni_env, g_lib_cls);
  jsr_audio_pause(0);
}

/* HOME and sleep freeze the whole process; the runtime's clocks find each
 * freeze whether or not focus messages came. What Android does around it:
 * the focus lost, then back. */
void port_process_frozen(unsigned count) {
  debugPrintf("[game] the process was held (HOME menu or sleep; freeze %u)\n", count);
  port_focus_lost();
  port_focus_gained();
}

/* ------------------------------------------------------------------- run */

/* initIDs() is not a normal native: its parameter is a java/lang/Object (an
 * INSTANCE -- classes.dex has initIDs(Ljava/lang/Object;)V), and its own code
 * takes GetObjectClass(thiz) and looks every method up on that class. Handed
 * g_lib_cls it gets java.lang.Class instead, all seventeen GetMethodID calls
 * return 0, and the engine then calls through method ID 0 -- and gets NULL back
 * from getBasePath(), so its paths (and every buffer it builds from them) are
 * rubbish. So: the instance. */
static void check_java_surface(JClass *c) {
  static const struct { const char *name, *sig; } k_want[] = {
      {"getBasePath", "()Ljava/lang/String;"},
      {"isLicensed", "()Z"},
      {"showBanner", "(I)V"},
      {"moreGames", "()V"},
      {"quitGame", "()V"},
      {"unlockAchievement", "(I)V"},
      {"isAchievementUnlocked", "(I)I"},
      {"updateLeaderboard", "(II)V"},
      {"startCamera", "()V"},
      {"startGallery", "()V"},
      {"shareImage", "()V"},
      {"playVideo", "(Ljava/lang/String;)V"},
      {"requestGC", "()V"},
      {"padConnected", "()I"},
      {"viewURL", "(Ljava/lang/String;)V"},
      {"goMarket", "()V"},
  };
  int missing = 0;
  for (unsigned i = 0; i < sizeof k_want / sizeof k_want[0]; i++) {
    JMethod *m = jni_method_best(c, k_want[i].name, k_want[i].sig, 1);
    if (!m || !m->def)
      missing++;
    debugPrintf("[java] %-24s %-28s %s\n", k_want[i].name, k_want[i].sig,
                (!m) ? "NOT FOUND" : (m->def ? "answered" : "NO HANDLER"));
  }
  debugPrintf("[java] %d of %u methods unhandled\n", missing,
              (unsigned)(sizeof k_want / sizeof k_want[0]));
}

/* The engine walks its own data and its own GL state before the first frame is
 * finished; where it dies is not in the log, only in crash.log, and the
 * registers there have already been reused. So the last few things the engine
 * asks for are noted: the files it opens (dcr_path_traced) and each GL wrapper
 * call that changes state. crash.log then has a name next to the abort. */
static int g_step_frames;

static void gl_note(const char *what) {
  debugPrintf("[gl] %s (frame %d)\n", what, g_step_frames);
}

#define GL_NOTE(expr, what)                  \
  do {                                      \
    expr;                                   \
    gl_note(what);                          \
  } while (0)

void port_run(void) {
  so_execute_init_array(&g_mod); /* System.loadLibrary: the constructors ... */
  jni_init();
  jsr_java_patch_monitors();     /* ... then the monitors lock for real */
  void *env = g_jni_env;

  /* The instance every native receives: the JetsetradioView built one of
   * these, and initIDs() takes GetObjectClass() of exactly this. */
  g_lib_obj = jni_new(JSR_CLS_LIB);
  g_lib_cls = jni_class(JSR_CLS_LIB)->obj;
  /* The engine's initIDs() starts with GetObjectClass() on exactly this object
   * and looks all sixteen of its methods up on the answer. That answer came back
   * NULL, and the engine does not stop for it (0x23a546 asserts and carries on
   * into the lookup loop), so it is worth seeing from the log what the receiver
   * looks like from this side and what GetObjectClass() makes of it. */
  {
    void *(*get_obj_class)(void *, void *) = (void *(*)(void *, void *))(*(void ***)g_jni_env)[31];
    void *c = g_lib_obj ? get_obj_class(env, g_lib_obj) : NULL;
    debugPrintf("[boot] the receiver is %p, GetObjectClass() says %p (%s)\n", g_lib_obj, c,
                c ? jni_class_of(c)->name : "null");
  }
  find_natives();

  jint (*on_load)(void *, void *) = (jint (*)(void *, void *))so_try_find_addr_rx(&g_mod, "JNI_OnLoad");
  if (on_load)
    debugPrintf("[boot] JNI_OnLoad -> 0x%x\n", (unsigned)on_load(g_jni_vm, NULL));

  jsr_input_init();

  /* The engine's own initIDs() caches the ids of the seventeen methods this
   * port answers. Checked here first, so the log says which one is missing
   * rather than leaving a method ID of 0 to be found out later. */
  check_java_surface(jni_class(JSR_CLS_LIB));
  void (*init_ids)(void *, void *) =
      (void (*)(void *, void *))so_try_find_addr_rx(&g_mod, "Java_com_sega_jetsetradio_JetsetradioLib_initIDs");
  if (init_ids) {
    debugPrintf("[boot] initIDs: starting (the engine looks its 17 methods up on our class)\n");
    init_ids(env, g_lib_obj);
    debugPrintf("[boot] initIDs: done\n");
  } else {
    debugPrintf("[boot] initIDs: not in this build of the library\n");
  }

  /* JetsetradioView on the console: the EGL surface and context before the
   * engine, which builds its shaders and buffers against them in init(). */
  if (!jsr_egl_ensure())
    fatal_error("No GLES 2 context could be made.\nSee debug.log.");

  int w = RT_SCREEN_W, h = RT_SCREEN_H;
  dcr_window_size(&w, &h);
  g_nat.init(env, g_lib_cls, w, h, 0);
  g_jsr_inited = 1;
  debugPrintf("[boot] the engine is up (%dx%d)\n", w, h);

  /* The sound starts after the engine has a mixer to fill. */
  jsr_audio_start();

  g_running = 1;
  port_focus_gained(); /* onResume */
  dcr_watchdog_start();
  debugPrintf("[boot] the game is running\n");

  int show_fps = rt_config_value("display.show_fps", 0);
  u64 fps_t0 = armGetSystemTick();
  unsigned fps_n = 0;
  u64 flush_t0 = fps_t0;
  int log_quiet = 0;
  int first_frame = 1;

  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (!rt_focused()) {
      dcr_boost_idle(); /* HOME/sleep: no frame runs, none of it is a load */
      svcSleepThread(50000000ll);
      continue;
    }
    if (jsr_timing_begin())
      jsr_input_poll();
    /* The engine's own GL calls cannot be traced from here, so the frame is
     * named from outside it: the first frames are where the engine has died
     * three times running, and with this the last line of the log before a
     * crash is the frame it died in. Past the eighth it is only noise. */
    if (g_step_frames < 8)
      gl_note("entering");
    jsr_native60_run(g_nat.step, env, g_lib_cls);
    if (g_step_frames++ < 8)
      gl_note("frame drawn");
    jsr_egl_swap();
    dcr_boost_poll(); /* a frame past 50 ms (a load off the SD) boosts until it ends */
    if (first_frame) {
      first_frame = 0;
      dcr_boost_launch_end();
    }
    /* Past the boot, the log goes to a RAM ring instead of the SD card on
     * every line: fewer stalls while playing. Written out every 10 s. */
    if (!log_quiet && dcr_gl_frames() > 180) {
      log_quiet = 1;
      dcr_io_report_readahead();
      dcr_io_report_patterns();
      log_set_quiet(1);
    }
    if (log_quiet && armTicksToNs(armGetSystemTick() - flush_t0) >= 10000000000ull) {
      flush_t0 = armGetSystemTick();
      dcr_boost_report();
      log_flush_ring();
    }
    if (show_fps) {
      /* Time-based: at load-time frame rates a frame-count window would take
       * minutes to report. */
      fps_n++;
      u64 ns = armTicksToNs(armGetSystemTick() - fps_t0);
      if (ns >= 2000000000ull) {
        debugPrintf("[fps] %.1f (%llu ms/frame)\n", (double)fps_n * 1e9 / (double)ns,
                    (unsigned long long)(ns / 1000000ull / (fps_n ? fps_n : 1)));
        fps_t0 = armGetSystemTick();
        fps_n = 0;
      }
    }
  }
  rt_applet_stop();
  g_running = 0;
  jsr_audio_stop();
  if (log_quiet)
    log_set_quiet(0); /* write out what the ring holds */
  log_flush_ring();
}
