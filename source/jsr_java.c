/* jsr_java.c -- the "Java" libjetsetradio.so calls.
 *
 * The engine's own initIDs() lists exactly what it asks of Java: it takes
 * GetObjectClass(thiz) and looks seventeen methods up on that one class --
 * there is no FindClass anywhere in the library and not a single
 * "Landroid/..." string in it. The engine is plain C++ underneath: its file
 * system goes through fopen/open/mkdir (bionic_io.c's shims and the runtime's
 * path translation), its GLES2 calls go straight to libGLESv2 (the EGL/GL
 * layer), and its sound is its own mixer (audioFillStreamBuffer).
 *
 * So this file is small: the seventeen methods, of which only getBasePath()
 * has anything to say. The rest are the vendor's hooks (a shop, Google Play,
 * achievements, leaderboards, the camera, videos, the Samsung S Pen, an
 * Applifier browser, an online licence check): nothing the game needs to run,
 * and each one answered honestly so the engine moves on. MIT.
 */
#include <string.h>

#include <switch.h>

#include "jsr.h"
#include "rt_cfg.h"
#include "rt_settings.h"
#include "util.h"

/* ------------------------------------------------------------- the engine */

/* getBasePath(): where the DATA archives and the CUSTOM folder live. Android
 * puts the expansion file under /sdcard/Android/obb/<pkg>/, but this wrapper
 * unpacks it once into the app's own files folder, which the runtime
 * translates to <game folder>/data/files -- an SD path the game can fopen
 * into, and one that needs no zip reader in the file system. */
static jvalue jsr_get_base_path(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self;
  (void)a;
  (void)m;
  return jv_l(jni_str(jsr_data_base()));
}

/* The vendor's hooks. Each is answered (so the engine sees a call that
 * returned) and, the first time, logged: the log then lists exactly which of
 * them the game reaches for, which is what a bug report needs. */
#define JSR_HOOK_V(name, sig)                                                                \
  static jvalue jsr_##name(JObj *self, const jvalue *a, const JMethod *m) {                  \
    (void)self; (void)a;                                                                      \
    debugPrintf("[java] %s%s called (a no-op here)\n", #name, sig);                          \
    return jv_none();                                                                         \
  }
#define JSR_HOOK_I(name, sig, val)                                                            \
  static jvalue jsr_##name(JObj *self, const jvalue *a, const JMethod *m) {                  \
    (void)self; (void)a;                                                                      \
    debugPrintf("[java] %s%s called (answered %d)\n", #name, sig, (int)(val));               \
    return jv_i(val);                                                                         \
  }

JSR_HOOK_V(showBanner, "(I)V")
JSR_HOOK_V(moreGames, "()V")
JSR_HOOK_V(quitGame, "()V")
JSR_HOOK_V(unlockAchievement, "(I)V")
JSR_HOOK_I(isAchievementUnlocked, "(I)I", 1)
JSR_HOOK_V(updateLeaderboard, "(II)V")
JSR_HOOK_V(shareImage, "()V")
/* The engine's own initIDs() is the authority on these, and it does not agree
 * with the vendor's classes.dex (or with the names a first reading suggests):
 * from the log of what it actually asks for --
 *   startCamera(II)V   startGallery(II)V   isLicensed()I
 * The dex has startCamera()/startGallery() with no arguments, and isLicensed()
 * returning Z. dex_names are the vendor's build; this library asks for
 * something else, and the library is what runs.
 *
 * A row with sig NULL answers the method whatever its signature is (the
 * engine only ever calls these through the id it looked up), and the
 * arguments it arrives with are read defensively: a startCamera() that turns
 * out to be startCamera(int, int) is still a startCamera. The jni.h note on a
 * NULL sig says exactly this -- "any signature", first match wins -- which is
 * why these rows come after nothing and read the argument count themselves. */
JSR_HOOK_V(startCamera, "(II)V")
JSR_HOOK_V(startGallery, "(II)V")
static jvalue jsr_isLicensed(JObj *self, const jvalue *arguments, const JMethod *method) {
  (void)self;
  (void)arguments;
  (void)method;
  static jint token;
  if (!rt_config_bool("game", "unlock_full_game"))
    return jv_i(0);
  if (!token && g_nat.licenseID) {
    token = g_nat.licenseID(g_jni_env, g_lib_cls);
    debugPrintf("[java] full-game license token initialized: %s\n", token ? "ok" : "unavailable");
  }
  return jv_i(token);
}
JSR_HOOK_V(playVideo, "(Ljava/lang/String;)V")
/* No Boehm GC is linked in; the engine asks for a collection now and then.
 * Refusing is safe: the runtime's own allocator is the one collecting. */
JSR_HOOK_V(requestGC, "()V")
static jvalue jsr_padConnected(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self;
  (void)a;
  (void)m;
  return jv_i(jsr_input_pad_connected());
}
JSR_HOOK_V(viewURL, "(Ljava/lang/String;)V")
JSR_HOOK_V(goMarket, "()V")

/* ------------------------------------------------------------- the tables */

const JMethodDef jni_method_defs[] = {
    /* Every method the engine's initIDs() asks for, with the signature IT asks
     * for (startCamera/startGallery take two ints here, isLicensed returns I --
     * the note above the hooks says why). The rows after these answer the
     * same names with any other signature, so a build that asks for the
     * classes.dex shape is answered too: nothing in these methods depends on
     * the arguments, and the engine calls them through the id it cached. */
    {JSR_CLS_LIB, "getBasePath", "()Ljava/lang/String;", jsr_get_base_path},
    {JSR_CLS_LIB, "isLicensed", "()I", jsr_isLicensed},
    {JSR_CLS_LIB, "showBanner", "(I)V", jsr_showBanner},
    {JSR_CLS_LIB, "moreGames", "()V", jsr_moreGames},
    {JSR_CLS_LIB, "quitGame", "()V", jsr_quitGame},
    {JSR_CLS_LIB, "unlockAchievement", "(I)V", jsr_unlockAchievement},
    {JSR_CLS_LIB, "isAchievementUnlocked", "(I)I", jsr_isAchievementUnlocked},
    {JSR_CLS_LIB, "updateLeaderboard", "(II)V", jsr_updateLeaderboard},
    {JSR_CLS_LIB, "startCamera", "(II)V", jsr_startCamera},
    {JSR_CLS_LIB, "startGallery", "(II)V", jsr_startGallery},
    {JSR_CLS_LIB, "shareImage", "()V", jsr_shareImage},
    {JSR_CLS_LIB, "playVideo", "(Ljava/lang/String;)V", jsr_playVideo},
    {JSR_CLS_LIB, "requestGC", "()V", jsr_requestGC},
    {JSR_CLS_LIB, "padConnected", "()I", jsr_padConnected},
    {JSR_CLS_LIB, "viewURL", "(Ljava/lang/String;)V", jsr_viewURL},
    {JSR_CLS_LIB, "goMarket", "()V", jsr_goMarket},
    /* The same names, any signature. */
    {JSR_CLS_LIB, "isLicensed", NULL, jsr_isLicensed},
    {JSR_CLS_LIB, "startCamera", NULL, jsr_startCamera},
    {JSR_CLS_LIB, "startGallery", NULL, jsr_startGallery},
    {JSR_CLS_LIB, "getBasePath", NULL, jsr_get_base_path},
    /* java.lang.Object: reached through a class the engine looks a method up
     * on (toString, hashCode, equals). */
    {"java/lang/Object", "toString", "()Ljava/lang/String;", jni_h_empty_string},
    {"java/lang/Object", "toString", NULL, jni_h_empty_string},
    {"java/lang/Object", "hashCode", "()I", jni_h_zero},
    {"java/lang/Object", "hashCode", NULL, jni_h_zero},
    {"java/lang/Object", "equals", "(Ljava/lang/Object;)Z", jni_h_false},
    {"java/lang/Object", "equals", NULL, jni_h_false},
    {NULL},
};

/* The class the natives are looked up on, and the one instance every native
 * gets as its receiver. */
JsrNative g_nat;
void *g_lib_cls;
JObj *g_lib_obj;
int g_jsr_inited;

/* The hierarchy the lookup walks. The engine only ever uses its own class, but
 * it takes GetObjectClass() of whatever it is handed, so Object must be there
 * for isSameObject/instanceof to walk. */
const char *const jni_class_supers[][2] = {
    {JSR_CLS_LIB, "java/lang/Object"},
    {"java/lang/String", "java/lang/Object"},
    {"java/lang/Class", "java/lang/Object"},
    {"java/lang/Integer", "java/lang/Object"},
    {"java/lang/Float", "java/lang/Object"},
    {"java/lang/Boolean", "java/lang/Object"},
    {"java/lang/Byte", "java/lang/Object"},
    {"java/lang/Short", "java/lang/Object"},
    {"java/lang/Long", "java/lang/Object"},
    {"java/lang/Double", "java/lang/Object"},
    {"java/nio/ByteBuffer", "java/lang/Object"},
    {"java/nio/ByteOrder", "java/lang/Object"},
    {"java/nio/Buffer", "java/lang/Object"},
    {NULL, NULL},
};

/* Used when the APK's class list (classes.txt) is not there yet. */
const char *const jni_missing_classes[] = {
    JSR_CLS_LIB, "java/lang/Object", "java/lang/String", "java/lang/Class",
    "java/lang/Integer", "java/lang/Float", "java/nio/ByteBuffer", NULL,
};

/* Nothing of the framework is needed, so nothing here is either. */
const JFieldDef jni_field_defs[] = {
    {NULL, NULL, NULL, 0, NULL},
};

/* ------------------------------------------------------------------ misc */

/* What the engine asks Java for, and what it does not get.
 *
 * jni_core answers every method it has a table entry for, and warns once for
 * each one nothing answers -- but a method it has never heard of comes back
 * NULL, and the engine stores that NULL and calls through it ("call through a
 * bad method ID 0x0", which is what the first boot showed, twice). A missing
 * method is therefore invisible until it is called, which is the worst moment.
 *
 * So every lookup the engine makes is logged here for the first few hundred,
 * saying plainly whether it was found. The names come from the engine itself,
 * not from a list written in advance. */
#define JNI_SLOT_GETMETHODID 33
#define JNI_SLOT_GETSTATICMETHODID 113
#define JNI_SLOT_TRACE_MAX 400

/* The engine's initIDs() does GetObjectClass() on the receiver and then looks
 * its sixteen methods up on what comes back. The receiver it gets here is not
 * an object the runtime can read, so GetObjectClass answers NULL -- and the
 * engine does not give up: 0x23a546 in libjetsetradio.so is an _myassert that
 * logs and falls straight back into the lookup loop, so all sixteen lookups
 * arrive with a null class and every one of them used to fail. Sixteen method
 * IDs of 0x0 in a row, and the engine then calls through them
 * ("call through a bad method ID 0x0") into whatever it got back -- which is
 * how the first frame ends up trying to copy to 0x20f80000.
 *
 * A method ID has to be a real one, so when the class does not arrive, the
 * lookup is repeated on JetsetradioLib: the only class in this game whose
 * methods the engine ever looks up. The name and signature are what identify
 * the method, and the engine calls it through the ID it was handed, so this
 * answers the right handler either way. Names that are none of ours are left
 * alone -- a method of some other class must not be answered as ours. */
static int is_our_method(const char *name) {
  for (int i = 0; jni_method_defs[i].cls; i++)
    if (jni_method_defs[i].name && !strcmp(jni_method_defs[i].name, name))
      return 1;
  return 0;
}

static void *(*g_real_get_method_id)(void *, void *, const char *, const char *);
static void *(*g_real_get_static_method_id)(void *, void *, const char *, const char *);
static int g_trace_left = JNI_SLOT_TRACE_MAX;
static int g_rescued;

static void jni_trace_lookup(const char *what, void *cls, const char *name, const char *sig, void *id) {
  if (g_trace_left <= 0)
    return;
  if (--g_trace_left == 0)
    debugPrintf("[java] (no more method lookups logged)\n");
  const char *cn = "<none>";
  if (cls) {
    JClass *c = jni_class_of(cls);
    cn = c ? c->name : "(not a class object)";
  }
  if (id)
    debugPrintf("[java] %s %s.%s%s -> ok\n", what, cn, name, sig);
  else
    debugPrintf("[java] %s %s.%s%s -> NOT FOUND (the engine will call a null id)\n", what, cn, name, sig);
}

static void *trace_get_method_id(void *env, void *cls, const char *name, const char *sig) {
  void *id = g_real_get_method_id(env, cls, name, sig);
  if (!id && !jni_class_of(cls) && is_our_method(name)) {
    id = g_real_get_method_id(env, jni_class(JSR_CLS_LIB)->obj, name, sig);
    if (id && g_rescued++ < 4)
      debugPrintf("[java]   (no class came with that one: answered on %s)\n", JSR_CLS_LIB);
  }
  jni_trace_lookup("GetMethodID", cls, name, sig, id);
  return id;
}

static void *trace_get_static_method_id(void *env, void *cls, const char *name, const char *sig) {
  void *id = g_real_get_static_method_id(env, cls, name, sig);
  if (!id && !jni_class_of(cls) && is_our_method(name)) {
    id = g_real_get_static_method_id(env, jni_class(JSR_CLS_LIB)->obj, name, sig);
    if (id && g_rescued++ < 4)
      debugPrintf("[java]   (no class came with that one: answered on %s)\n", JSR_CLS_LIB);
  }
  jni_trace_lookup("GetStaticMethodID", cls, name, sig, id);
  return id;
}

/* JNIEnv's MonitorEnter and MonitorExit, which the runtime answers without
 * locking anything (jni_core.c: both return 0). The engine's loader threads
 * share buffers through a monitor, exactly as Gameloft's did on the ports
 * this runtime came from: without the lock a model loads as nothing, a size
 * of a gigabyte, and a fault somewhere in the loader on some starts and not on
 * others. One lock per object locked (few, and never let go), patched into
 * the JNI table's standard slots (217/218 of the 233-entry
 * JNINativeInterface). Uncontended it costs one mutex round-trip. */
#define JNI_MONITOR_ENTER 217
#define JNI_MONITOR_EXIT  218
#define MAX_MONITORS 16

static struct {
  const void *obj;
  RMutex lock;
} g_monitors[MAX_MONITORS];
static int g_nmonitors;
static Mutex g_monitors_lock;

static RMutex *monitor_of(const void *obj) {
  mutexLock(&g_monitors_lock);
  int i = 0;
  while (i < g_nmonitors && g_monitors[i].obj != obj)
    i++;
  if (i == MAX_MONITORS) {
    i--; /* more objects than locks: the last one is shared */
  } else if (i == g_nmonitors) {
    g_monitors[i].obj = obj;
    rmutexInit(&g_monitors[i].lock);
    g_nmonitors++;
    debugPrintf("[java] a monitor for %p\n", obj);
  }
  mutexUnlock(&g_monitors_lock);
  return &g_monitors[i].lock;
}

static jint monitor_enter(void *env, void *obj) {
  (void)env;
  rmutexLock(monitor_of(obj));
  return 0;
}

static jint monitor_exit(void *env, void *obj) {
  (void)env;
  rmutexUnlock(monitor_of(obj));
  return 0;
}

void jsr_java_patch_monitors(void) {
  mutexInit(&g_monitors_lock);
  void **env = *(void ***)g_jni_env; /* the function table */
  env[JNI_MONITOR_ENTER] = (void *)monitor_enter;
  env[JNI_MONITOR_EXIT] = (void *)monitor_exit;
  /* From here on every method the engine looks up is logged, found or not.
   *
   * The originals are taken BEFORE the slots are overwritten: reading a slot
   * back afterwards would find this hook already installed, so the "original"
   * would be the hook -- an immediate infinite recursion the first time the
   * engine asked for anything. (The build that did that crashed inside initIDs
   * with a stack overflow.) */
  g_real_get_method_id = (void *(*)(void *, void *, const char *, const char *))env[JNI_SLOT_GETMETHODID];
  g_real_get_static_method_id = (void *(*)(void *, void *, const char *, const char *))env[JNI_SLOT_GETSTATICMETHODID];
  env[JNI_SLOT_GETMETHODID] = (void *)trace_get_method_id;
  env[JNI_SLOT_GETSTATICMETHODID] = (void *)trace_get_static_method_id;
  debugPrintf("[java] method lookups are logged from here\n");
}
