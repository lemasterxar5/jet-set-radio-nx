/* jsr_input.c -- the touchscreen, and the Joy-Con as one.
 *
 * Jet Set Radio accepts native controller state through updateInput().
 * padConnected() reports the current Switch controller connection.
 * The engine takes touches in batches:
 *
 *   startSendingTouches()          a frame with touches is coming
 *   sendTouch(action, id, x, y, pressure, size)
 *   endSendingTouches()
 *
 * so the wrapper brackets every frame it has something to send in, and sends
 * nothing at all when the player touched nothing (which is most frames).
 * Action: 0 down, 1 up, 2 move -- what MotionEvent would say, with 2 for
 * ACTION_MOVE; id is a finger number, 0 first.
 *
 * On the console the touch screen is the real thing and needs nothing else.
 * Docked the player holds a controller, so [controls] in config.ini gives a
 * Joy-Con button a point of the on-screen layout to press. The points are in
 * a 16:9 layout space [controls] layout_height pixels tall (720 = the
 * default 1280x720) and are scaled to whatever resolution is running. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "jsr.h"
#include "rt_cfg.h"
#include "rt_pad.h"
#include "rt_settings.h"
#include "rt_window.h"
#include "util.h"

/* Android's MotionEvent actions, which is what the engine was written against. */
#define A_DOWN 0
#define A_UP 1
#define A_MOVE 2

#define MAX_TOUCHES 16
#define MAX_BTNS 10

typedef struct {
  const char *name;
  u64 buttons;
  int x, y; /* in the layout space */
} TouchBtn;

/* The table config.ini is written from: the names [controls] answers to. */
static const struct {
  const char *name;
  u64 buttons;
} k_names[MAX_BTNS] = {
    {"a", HidNpadButton_A},
    {"b", HidNpadButton_B},
    {"x", HidNpadButton_X},
    {"y", HidNpadButton_Y},
    {"l", HidNpadButton_L},
    {"r", HidNpadButton_R},
    {"zl", HidNpadButton_ZL},
    {"zr", HidNpadButton_ZR},
    {"l_stick", HidNpadButton_StickL},
    {"r_stick", HidNpadButton_StickR},
};

static TouchBtn g_btns[MAX_BTNS];
static int g_nbtns;
static int g_layout_h = 720; /* the layout space's height in points */
static int g_w = RT_SCREEN_W, g_h = RT_SCREEN_H; /* the game's rendering size */
static int g_finger_down;                       /* the screen's finger: id 0 */
static int g_btn_down[MAX_BTNS];                /* per mapped button */
static int g_last_x, g_last_y;

static PadState g_pad;
static u64 g_pad_prev;
static float g_sticks[4];
static int g_pad_ready;
static int g_native_pad;
static int g_connected;
static int g_native_state[13];
static int g_native_last_connected = -1;

int jsr_input_pad_connected(void) {
  return g_native_pad && g_connected;
}

static int native_axis(float value) {
  if (value >= -0.25f && value <= 0.25f)
    return 0;
  if (value > 1.0f)
    value = 1.0f;
  if (value < -1.0f)
    value = -1.0f;
  return (int)(value * 127.0f);
}

static void send_native(u64 buttons) {
  int state[13] = {0};
  if (g_connected) {
    state[0] = !!(buttons & HidNpadButton_Left);
    state[1] = !!(buttons & HidNpadButton_Right);
    state[2] = !!(buttons & HidNpadButton_Up);
    state[3] = !!(buttons & HidNpadButton_Down);
    state[4] = !!(buttons & HidNpadButton_A);
    state[5] = !!(buttons & HidNpadButton_B);
    state[6] = !!(buttons & (HidNpadButton_L | HidNpadButton_ZL));
    state[7] = !!(buttons & (HidNpadButton_R | HidNpadButton_ZR));
    state[8] = !!(buttons & HidNpadButton_Plus);
    state[9] = native_axis(g_sticks[0]);
    state[10] = native_axis(-g_sticks[1]);
    state[11] = native_axis(g_sticks[2]);
    state[12] = native_axis(-g_sticks[3]);
  }
  int changed = memcmp(state, g_native_state, sizeof state) != 0 ||
                g_connected != g_native_last_connected;
  g_nat.updateInput(g_jni_env, g_lib_cls, changed,
                    state[0], state[1], state[2], state[3], state[4], state[5],
                    state[6], state[7], state[8], state[9], state[10], state[11], state[12]);
  memcpy(g_native_state, state, sizeof state);
  if (g_connected != g_native_last_connected)
    debugPrintf("[input] MOGA native controller %s; left stick = movement, right stick = camera\n",
                g_connected ? "connected" : "disconnected");
  g_native_last_connected = g_connected;
}

/* What changed this frame, gathered before the batch is opened. */
typedef struct {
  int action, id, x, y;
} Ev;
static Ev g_ev[MAX_TOUCHES * 2];

static void push(int *n, int action, int id, int x, int y) {
  if (*n >= (int)(sizeof g_ev / sizeof g_ev[0]))
    return;
  g_ev[*n].action = action;
  g_ev[*n].id = id;
  g_ev[*n].x = x;
  g_ev[*n].y = y;
  (*n)++;
}

static void load_layout(void) {
  g_nbtns = 0;
  g_layout_h = rt_config_int("controls", "layout_height");
  if (g_layout_h < 240 || g_layout_h > 4320)
    g_layout_h = 720;
  for (int i = 0; i < MAX_BTNS; i++) {
    char key[32];
    const char *sx, *sy;
    snprintf(key, sizeof key, "%s_x", k_names[i].name);
    sx = rt_config_get("controls", key);
    snprintf(key, sizeof key, "%s_y", k_names[i].name);
    sy = rt_config_get("controls", key);
    if (!sx || !sy || !*sx || !*sy)
      continue;
    g_btns[g_nbtns].name = k_names[i].name;
    g_btns[g_nbtns].buttons = k_names[i].buttons;
    g_btns[g_nbtns].x = atoi(sx);
    g_btns[g_nbtns].y = atoi(sy);
    g_nbtns++;
  }
  debugPrintf("[input] %d mapped button(s), layout %dx%d\n", g_nbtns, g_layout_h * 16 / 9, g_layout_h);
}

/* A layout point in the game's current pixel space. */
static void scale_point(const TouchBtn *b, int *x, int *y) {
  *x = b->x * g_w / (g_layout_h * 16 / 9);
  *y = b->y * g_h / g_layout_h;
}

static void send(int action, int id, int x, int y) {
  if (!g_nat.sendTouch || !g_lib_obj)
    return;
  g_nat.sendTouch(g_jni_env, g_lib_cls, action, id, (float)x, (float)y, 1.0f, 1.0f);
}

/* One press: the point goes down, and up again on the same frame's release. */
static void tap(int x, int y) {
  send(A_DOWN, 0, x, y);
  send(A_UP, 0, x, y);
}

void jsr_input_init(void) {
  dcr_window_size(&g_w, &g_h);
  g_pad_ready = rt_pad_setup(1, 1) == 0;
  padInitializeDefault(&g_pad);
  padUpdate(&g_pad);
  g_connected = g_pad_ready && padIsConnected(&g_pad);
  g_pad_prev = 0;
  g_native_last_connected = -1;
  memset(g_native_state, 0, sizeof g_native_state);
  g_native_pad = rt_config_bool("controls", "moga") && g_nat.updateInput != NULL;
  if (!g_native_pad)
    load_layout();
  debugPrintf("[input] control mode: %s\n", g_native_pad ? "MOGA native" : "touch layout");
}

void jsr_input_poll(void) {
  if (!g_lib_obj)
    return;

  /* What changed this frame is gathered first, then sent inside one
   * startSendingTouches()/endSendingTouches() batch: the engine expects
   * nothing outside a batch. */
  int n = 0;
  Ev *ev = g_ev;
  int x = 0, y = 0;

  /* The console's touch screen is always 1280x720; the game draws in
   * g_w x g_h. The first finger is id 0 -- the real one, which a virtual
   * press never takes away. */
  HidTouchScreenState ts;
  memset(&ts, 0, sizeof ts);
  hidGetTouchScreenStates(&ts, 1);
  if (ts.count) {
    int down = ts.touches[0].x != 0 || ts.touches[0].y != 0;
    x = (int)ts.touches[0].x * g_w / 1280;
    y = (int)ts.touches[0].y * g_h / 720;
    if (down != g_finger_down) {
      g_finger_down = down;
      push(&n, down ? A_DOWN : A_UP, 0, x, y);
    } else if (down && (x != g_last_x || y != g_last_y)) {
      push(&n, A_MOVE, 0, x, y);
    }
    g_last_x = x;
    g_last_y = y;
  } else if (g_finger_down) {
    g_finger_down = 0;
    push(&n, A_UP, 0, g_last_x, g_last_y);
  }

  if (g_pad_ready) {
    padUpdate(&g_pad);
    u64 now = rt_pad_read(&g_pad, g_sticks);
    g_connected = padIsConnected(&g_pad);
    if (g_native_pad)
      send_native(now);
    u64 pressed = now & ~g_pad_prev;
    u64 released = ~now & g_pad_prev;
    g_pad_prev = now;
    /* The system's own buttons are not the layout's. */
    if (pressed & HidNpadButton_Minus) {
      if (g_nat.backPressed)
        g_nat.backPressed(g_jni_env, g_lib_cls);
    }
    if (!g_native_pad && rt_config_bool("controls", "buttons")) {
      for (int i = 0; i < g_nbtns; i++) {
        TouchBtn *b = &g_btns[i];
        int want = g_btn_down[i];
        if (pressed & b->buttons)
          want = 1;
        else if (released & b->buttons)
          want = 0;
        if (want == g_btn_down[i])
          continue;
        g_btn_down[i] = want;
        scale_point(b, &x, &y);
        push(&n, want ? A_DOWN : A_UP, 1 + i, x, y);
      }
    }
  }

  if (!n || !g_nat.startSendingTouches || !g_nat.endSendingTouches)
    return;
  g_nat.startSendingTouches(g_jni_env, g_lib_cls);
  for (int i = 0; i < n; i++)
    send(ev[i].action, ev[i].id, ev[i].x, ev[i].y);
  g_nat.endSendingTouches(g_jni_env, g_lib_cls);
}

/* A press at a point, as one frame's batch (a menu the player reached for
 * with a button, say). */
void jsr_input_tap(int x, int y) {
  if (!g_nat.startSendingTouches || !g_lib_obj)
    return;
  g_nat.startSendingTouches(g_jni_env, g_lib_cls);
  tap(x, y);
  g_nat.endSendingTouches(g_jni_env, g_lib_cls);
}
