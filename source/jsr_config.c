/* jsr_config.c -- config.ini: the settings, and the table that writes them.
 *
 * The runtime reads [display] resolution and [performance]
 * boost_cpu_when_loading, and [debug] gl_selftest / log_java_calls, through
 * the rows below that carry a dst. The rest are the port's: the ones jsr_input
 * and jsr_main read are listed here without a dst (rt_config_get() reads
 * them), which is also how the file gets its help text. MIT.
 */
#include <string.h>

#include "jsr.h"
#include "rt_cfg.h"
#include "rt_settings.h"
#include "rt_window.h"
#include "util.h"

typedef struct JsrConfig {
  int resolution;    /* 0 auto, 1 = 1280x720, 2 = 1024x576 */
  int frame_rate;    /* 0 = 60, 1 = 30 */
  int show_fps;
  int crosshair;
  int boost_loading;
  int gpu_boost;
  int gl_selftest;
  int log_java;
  int boot_log;
  int layout_height; /* the touch layout's space, in points (see jsr_input.c) */
  int buttons;       /* a Joy-Con button presses the layout */
} JsrConfig;

static JsrConfig g_cfg = {
    .resolution = 0, .frame_rate = 0, .show_fps = 0, .crosshair = 1,
    .boost_loading = 1, .gpu_boost = 1, .gl_selftest = 0, .log_java = 0,
    .boot_log = 0, .layout_height = 720, .buttons = 1,
};

/* A row with a dst (the fields rt_config() and the port read directly). The
 * tail is variadic because the help text is a string that may hold commas. */
#define ROW(sec, key, def, help, ...) {(sec), (key), (def), (help), __VA_ARGS__}
/* A row without one: read with rt_config_get()/rt_config_int(). */
#define ROW_R(sec, key, def, help, ...) {(sec), (key), (def), (help), __VA_ARGS__}
/* ... with this dst. */
#define F(field) CFG_BOOL, NULL, &(g_cfg.field)
/* ... with this dst and these choices. */
#define FC(field, choices) CFG_CHOICE, (choices), &(g_cfg.field)

#ifdef JSR_NATIVE60_DEFAULT
#define NATIVE60_DEFAULT "true"
#else
#define NATIVE60_DEFAULT "false"
#endif

static const CfgOpt k_opts[] = {
    ROW_R("game", "unlock_full_game", "true",
          "# Initialize the native full-game license handshake; does not unlock save progress.", CFG_BOOL, NULL),
    ROW_R("display", "draw_distance", "2.5",
          "# Object visibility multiplier (1 = original). Higher values can reduce FPS.",
          CFG_FLOAT, NULL, NULL, 1.0f, 2.5f),
    ROW_R("display", "detail_distance", "1.5",
          "# Keep detailed object models/textures farther away (1 = original).",
          CFG_FLOAT, NULL, NULL, 1.0f, 2.0f),
    ROW("display", "resolution", "auto",
        "# Rendering resolution. 'auto' follows the mode the console is in;\n"
        "# 720 is the native docked mode, and runs cooler in handheld.",
        FC(resolution, "auto,1280,1024")),
    ROW("display", "frame_rate", "60",
        "# Target presentation rate. With interpolation, 60 draws intermediate\n"
        "# poses while the original game logic stays at 30 Hz during gameplay.",
        FC(frame_rate, "60,30")),
    ROW_R("display", "interpolate", "true",
          "# Experimental 60 FPS rendering with 30 Hz simulation.\n"
          "# Disable to follow the APK's original 30 FPS gameplay limit.", CFG_BOOL, NULL),
    ROW_R("display", "native60", NATIVE60_DEFAULT,
          "# Experimental engine-time adapters for APK 1.07; overrides interpolate.\n"
          "# Live input per frame; recorded commands/actors/cameras and stage clock at 30 Hz.\n"
          "# Not a fully converted 60 Hz engine; physics and transitions need testing.", CFG_BOOL, NULL),
    ROW("display", "show_fps", "false", "# An [fps] line every two seconds.", F(show_fps)),
    ROW_R("display", "fps_overlay", "false",
          "# Rendered frames per second in the top-left corner; updated twice a second.\n"
          "# Counts presentation, not the original 30 Hz gameplay simulation.", CFG_BOOL, NULL),
    ROW("display", "crosshair", "true", "# A small crosshair in the middle of the screen.", F(crosshair)),
    ROW_R("display", "decode_etc1", "false",
          "# CPU-decode ETC1 textures to RGB8 for emulators with broken ETC support.\n"
          "# Enable in Citron; leave disabled on Switch to keep native compression.", CFG_BOOL, NULL),

    ROW("performance", "boost_cpu_when_loading", "true",
        "# CPU at full speed while the game streams hundreds of MB off the SD\n"
        "# card. Gameplay stays at the console's stock clocks.",
        F(boost_loading)),
    ROW("performance", "gpu_boost_handheld", "true",
        "# Handheld GPU at 460.8 MHz (CPU and memory stay at stock; docked is\n"
        "# untouched).", F(gpu_boost)),

    ROW_R("jsr", "library", "libjetsetradio.so",
          "# Which of the APK's two builds to load. Both hold the same game, the\n"
          "# same natives and the same data; 'libjetsetradio-nn.so' is the\n"
          "# larger of the two.",
          CFG_TEXT, NULL),

    ROW_R("controls", "moga", "true",
          "# Native MOGA-style input. Left stick moves; right stick controls the camera.\n"
          "# A jumps, B backs out, ZL sprays, L centers the camera, R/ZR dashes, + pauses.\n"
          "# With false, use the legacy touch-coordinate button layout.", CFG_BOOL, NULL),
    ROW("controls", "layout_height", "720",
        "# The height of the touch layout the button points below are written\n"
        "# in (720 = the default 1280x720 rendering). Change it and they are\n"
        "# scaled to whatever resolution is running.",
        CFG_INT, "720,540,1080", &(g_cfg.layout_height)),
    ROW("controls", "buttons", "true",
        "# Legacy button-to-touch mapping, used only when moga=false.",
        F(buttons)),

    /* The points. The engine draws this same layout on the touch screen
     * (CUSTOM/controls_*.png); these are where a button presses it. x from
     * the left, y from the top, in the layout space above. */
    ROW_R("controls", "a_x", "0", "# A.", CFG_INT, NULL),
    ROW_R("controls", "a_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "b_x", "0", "# B.", CFG_INT, NULL),
    ROW_R("controls", "b_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "x_x", "0", "# X.", CFG_INT, NULL),
    ROW_R("controls", "x_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "y_x", "0", "# Y.", CFG_INT, NULL),
    ROW_R("controls", "y_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "l_x", "0", "# L.", CFG_INT, NULL),
    ROW_R("controls", "l_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "r_x", "0", "# R.", CFG_INT, NULL),
    ROW_R("controls", "r_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "zl_x", "0", "# ZL.", CFG_INT, NULL),
    ROW_R("controls", "zl_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "zr_x", "0", "# ZR.", CFG_INT, NULL),
    ROW_R("controls", "zr_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "l_stick_x", "0", "# Left stick click.", CFG_INT, NULL),
    ROW_R("controls", "l_stick_y", "0", NULL, CFG_INT, NULL),
    ROW_R("controls", "r_stick_x", "0", "# Right stick click.", CFG_INT, NULL),
    ROW_R("controls", "r_stick_y", "0", NULL, CFG_INT, NULL),

    ROW("debug", "gl_selftest", "false", "# Run the renderer's own self-test at start-up.", F(gl_selftest)),
    ROW("debug", "boot_log_on_screen", "false", "# The boot log on the screen.", F(boot_log)),
    ROW("debug", "log_java_calls", "false",
        "# Log every call the engine makes into Java (slow: for bug reports).", F(log_java)),
};

static const CfgSection k_sections[] = {
    {"controls",
     "# Native MOGA input is enabled by default. The touch screen remains available.\n"
     "# Touch coordinates below apply only to the legacy moga=false mode."},
    {"jsr", "# The game's own library, taken from your APK. Nothing to change here\n"
            "# unless one of the two builds runs better on your console."},
    {"debug", "# For bug reports: keep debug.log and send it with them."},
};

static void apply(void) {
  int w = 1280, h = 720;
  if (g_cfg.resolution == 1) {
    w = 1280, h = 720;
  } else if (g_cfg.resolution == 2) {
    w = 1024, h = 576;
  } else {
    /* auto: the mode the console is in, when docked; 1280x720 in handheld. */
    w = 1280, h = 720;
  }
  dcr_window_set_size(w, h);
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .sections = k_sections,
    .nsections = CFG_COUNT(k_sections),
    .migrate = (const CfgMigrate[]){{"display", "draw_distance", "1.5", "2.5", 2}},
    .nmigrate = 1,
    .version = 2,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
