#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "jsr.h"
#include "jsr_collision60.h"
#include "jsr_engine_fall.h"
#include "jsr_event60.h"
#include "jsr_native60.h"
#include "jsr_pose60.h"
#include "rt_applet.h"
#include "rt_cfg.h"
#include "util.h"

#define SECOND_NS 1000000000ull
#define PLAYER_SLOTS 16
#define PLAYER_TIMERS 9
#define SCRIPT_SLOTS 32

uintptr_t jsr_n60_motion_tail, jsr_n60_motion_ex_tail;
uintptr_t jsr_n60_angle_tail, jsr_n60_scalar_tail, jsr_n60_speed_tail, jsr_n60_turn_tail;
uintptr_t jsr_n60_ride_tail, jsr_n60_jump_tail, jsr_n60_grind_tail, jsr_n60_wall_tail;
uintptr_t jsr_n60_fall_tail, jsr_n60_stage_tail, jsr_n60_fall_block_tail;
uintptr_t jsr_n60_dispatch_first_tail, jsr_n60_dispatch_second_tail;
uint32_t jsr_n60_stage_literals[2];

extern uintptr_t jsr_exec_literal, jsr_exec_tail, jsr_timer_count;
extern void jsr_original_exec(void);
extern int jsr_n60_original_motion(const void *, float *, float, int);
extern int jsr_n60_original_motion_ex(const void *, float *, float, float, float, int);
extern int jsr_n60_original_angle(int, int, int);
extern float jsr_n60_original_scalar(float, float, float);
extern void jsr_n60_original_speed(void *, int);
extern void jsr_n60_original_turn(void *, int, float);
extern void jsr_n60_original_ride(void *, void *);
extern void jsr_n60_original_jump(void *);
extern void jsr_n60_original_grind(void *, void *);
extern void jsr_n60_original_wall(void *);
extern void jsr_n60_original_fall(void *);
extern void jsr_n60_original_stage(void);
extern void jsr_n60_dispatch_first(void);
extern void jsr_n60_dispatch_second(void);
extern void jsr_n60_fall_block_entry(void);

typedef struct {
  JsrEngineCountdown clock;
  int32_t expected;
} PlayerTimer;

typedef struct Player {
  void *player;
  struct Player *next;
  uint64_t frame;
  uint32_t state;
  PlayerTimer timers[PLAYER_TIMERS];
  JsrEngineCountdown fall_clock;
  int32_t fall_expected;
  float vertical_correction, horizontal_correction;
} Player;

typedef struct ScriptClock {
  void *object;
  struct ScriptClock *next;
  uint64_t frame;
  uint64_t sampled_frame;
  int64_t remainder;
  unsigned steps;
  uint8_t previous_buttons;
  int has_buttons;
  void *recorded_player;
  unsigned traces;
  uintptr_t traced_action;
  unsigned kind;
  int32_t state;
} ScriptClock;

typedef struct {
  const char *name;
  uintptr_t offset;
  uint8_t prefix[10];
  unsigned length;
  void *replacement;
  uintptr_t *tail;
} NativeHook;

static const unsigned g_timer_offsets[PLAYER_TIMERS] = {
    0xf0, 0x10c, 0x110, 0x510, 0x5d4, 0x880, 0x888, 0x884, 0x890,
};
static JsrEngineClock g_clock;
static Player g_players[PLAYER_SLOTS];
static Player *g_player_overflow;
static ScriptClock g_scripts[SCRIPT_SLOTS];
static ScriptClock *g_script_overflow;
static uintptr_t g_player_callback, g_camera_callback;
static uintptr_t g_key_callback, g_practice_callback;
static uintptr_t g_demo_callback, g_scene_callback, g_radio_callback;
static uintptr_t g_lesson_callback, g_event_manager_callback, g_event_telop_callback;
static uintptr_t g_game_base;
static void *(*g_action_get)(unsigned);
static int g_recorded_world_step, g_recorded_world_active;
static unsigned long g_recorded_world_ticks;
static const struct {
  uintptr_t offset;
  uint8_t prefix[8];
} g_graffiti_profiles[] = {
    {0x1e3fd0, {0xc3,0x6e,0xd3,0xf8,0x48,0x21,0x09,0x32}},
    {0x1e3f7c, {0x10,0xb5,0xc3,0x6e,0x04,0x46,0xd3,0xf8}},
    {0x1e3f30, {0x10,0xb5,0xc3,0x6e,0x04,0x46,0xd3,0xf8}},
    {0x1e4f24, {0x2d,0xe9,0xf8,0x43,0x04,0x46,0x2d,0xed}},
    {0x1e51c4, {0xf8,0xb5,0x06,0x46,0x6a,0x20,0xca,0xf7}},
    {0x1e4ed0, {0x10,0xb5,0x83,0x69,0x04,0x46,0x1b,0x6f}},
};
static uint32_t *g_area;
static uint32_t g_last_area;
static uint64_t g_frame, g_deadline, g_present_remainder;
static unsigned g_present_hz;
static int g_requested, g_installed, g_active, g_scaled, g_collision_depth;
static void *g_falling;
static float g_fall_gravity;
static unsigned long g_fast_calls, g_legacy_calls, g_motion_calls, g_collision_calls;
static unsigned long g_recorded_calls, g_recorded_holds;
static unsigned long g_collision_hold_failures;

static float read_float(const void *object, unsigned offset) {
  float value;
  memcpy(&value, (const uint8_t *)object + offset, sizeof value);
  return value;
}

static void write_float(void *object, unsigned offset, float value) {
  memcpy((uint8_t *)object + offset, &value, sizeof value);
}

static int32_t read_int(const void *object, unsigned offset) {
  int32_t value;
  memcpy(&value, (const uint8_t *)object + offset, sizeof value);
  return value;
}

static void write_int(void *object, unsigned offset, int32_t value) {
  memcpy((uint8_t *)object + offset, &value, sizeof value);
}

static Player *player_at(void *object, int create) {
  Player *available = NULL;
  for (unsigned index = 0; index < PLAYER_SLOTS; index++) {
    Player *player = &g_players[index];
    if (player->player == object)
      return player;
    if (!available && (!player->player || player->frame + 120 < g_frame))
      available = player;
  }
  for (Player **link = &g_player_overflow; *link;) {
    Player *player = *link;
    if (player->player == object)
      return player;
    if (player->frame + 120 < g_frame) {
      *link = player->next;
      free(player);
    } else {
      link = &player->next;
    }
  }
  if (!create)
    return NULL;
  if (!available) {
    available = calloc(1, sizeof *available);
    if (!available)
      return NULL;
    available->next = g_player_overflow;
    g_player_overflow = available;
  }
  Player *next = available->next;
  memset(available, 0, sizeof *available);
  available->next = next;
  available->player = object;
  return available;
}

int jsr_native60_requested(void) {
  return g_requested;
}

void jsr_native60_reset(void) {
  jsr_audio_catchup(0);
  while (g_script_overflow) {
    ScriptClock *clock = g_script_overflow;
    g_script_overflow = clock->next;
    free(clock);
  }
  while (g_player_overflow) {
    Player *player = g_player_overflow;
    g_player_overflow = player->next;
    free(player);
  }
  jsr_collision60_reset();
  jsr_pose60_reset();
  jsr_engine_clock_reset(&g_clock);
  memset(g_players, 0, sizeof g_players);
  memset(g_scripts, 0, sizeof g_scripts);
  g_deadline = g_present_remainder = g_frame = 0;
  g_present_hz = 0;
  g_active = g_scaled = g_collision_depth = 0;
  g_recorded_world_step = g_recorded_world_active = 0;
  g_recorded_world_ticks = 0;
  g_falling = NULL;
}

static int hooked_motion(const void *motion, float *frame, float step, int flags) {
  jsr_pose60_motion(motion, *frame, step, flags, 0, 0.0f, 0.0f);
  if (g_scaled) {
    g_motion_calls++;
    step = jsr_engine_clock_scale(&g_clock, step);
  }
  return jsr_n60_original_motion(motion, frame, step, flags);
}

static int hooked_motion_ex(const void *motion, float *frame, float step,
                             float start, float end, int flags) {
  jsr_pose60_motion(motion, *frame, step, flags, 1, start, end);
  if (g_scaled) {
    g_motion_calls++;
    step = jsr_engine_clock_scale(&g_clock, step);
  }
  return jsr_n60_original_motion_ex(motion, frame, step, start, end, flags);
}

static int hooked_angle(int current, int target, int step) {
  if (g_scaled && step > 0) {
    double scaled = (double)step * g_clock.scale;
    step = scaled >= INT_MAX ? INT_MAX : (int)scaled;
  }
  return jsr_n60_original_angle(current, target, step);
}

static float hooked_scalar(float current, float target, float step) {
  if (g_scaled)
    step = jsr_engine_clock_scale(&g_clock, step);
  return jsr_n60_original_scalar(current, target, step);
}

static void hooked_speed(void *object, int mode) {
  float previous = read_float(object, 0xdc);
  jsr_n60_original_speed(object, mode);
  if (g_scaled) {
    float current = read_float(object, 0xdc);
    if (isfinite(previous) && isfinite(current)) {
      write_float(object, 0xdc, previous + (current - previous) * g_clock.scale);
      Player *player = player_at(object, 0);
      if (player)
        player->horizontal_correction += (current - previous) *
            (0.5f * g_clock.scale * (1.0f - g_clock.scale));
    }
  }
}

static void hooked_turn(void *object, int angle, float ratio) {
  float previous = read_float(object, 0x1d8);
  jsr_n60_original_turn(object, angle, ratio);
  if (g_scaled) {
    float current = read_float(object, 0x1d8);
    if (isfinite(previous) && isfinite(current))
      write_float(object, 0x1d8, previous + (current - previous) * g_clock.scale);
  }
}

static void prepare_timers(Player *player, int32_t previous[PLAYER_TIMERS],
                            int32_t prepared[PLAYER_TIMERS]) {
  uint32_t state = (uint32_t)read_int(player->player, 0x6c);
  if (player->state != state) {
    memset(player->timers, 0, sizeof player->timers);
    jsr_engine_countdown_reset(&player->fall_clock);
    player->fall_expected = 0;
    player->state = state;
  }
  unsigned fall_state = (unsigned)read_int(player->player, 0x204);
  for (unsigned index = 0; index < PLAYER_TIMERS; index++) {
    PlayerTimer *timer = &player->timers[index];
    int32_t value = read_int(player->player, g_timer_offsets[index]);
    previous[index] = prepared[index] = value;
    if (value != timer->expected)
      jsr_engine_countdown_reset(&timer->clock);
    if (!index && (fall_state == 1 || fall_state == 3))
      continue;
    if (value > 0 && value < INT_MAX) {
      int32_t target = value;
      jsr_engine_countdown_advance(&g_clock, &timer->clock, &target);
      prepared[index] = target + 1;
      write_int(player->player, g_timer_offsets[index], prepared[index]);
    }
  }
}

static void finish_timers(Player *player, const int32_t previous[PLAYER_TIMERS],
                           const int32_t prepared[PLAYER_TIMERS]) {
  for (unsigned index = 0; index < PLAYER_TIMERS; index++) {
    int32_t value = read_int(player->player, g_timer_offsets[index]);
    if (prepared[index] != previous[index] && value == prepared[index]) {
      value = previous[index];
      write_int(player->player, g_timer_offsets[index], value);
    }
    player->timers[index].expected = value;
  }
}

static ScriptClock *script_at(void *object, unsigned kind, int32_t state) {
  ScriptClock *available = NULL;
  for (unsigned index = 0; index < SCRIPT_SLOTS; index++) {
    ScriptClock *clock = &g_scripts[index];
    if (clock->object == object && clock->kind == kind) {
      if (clock->state != state || clock->frame + 1 < g_frame) {
        clock->remainder = 0;
        clock->sampled_frame = UINT64_MAX;
        clock->has_buttons = 0;
        clock->recorded_player = NULL;
      }
      clock->state = state;
      clock->frame = g_frame;
      return clock;
    }
    if (!available && (!clock->object || clock->frame + 120 < g_frame))
      available = clock;
  }
  for (ScriptClock **link = &g_script_overflow; *link;) {
    ScriptClock *clock = *link;
    if (clock->object == object && clock->kind == kind) {
      if (clock->state != state || clock->frame + 1 < g_frame) {
        clock->sampled_frame = UINT64_MAX;
        clock->has_buttons = 0;
        clock->recorded_player = NULL;
      }
      clock->state = state;
      clock->frame = g_frame;
      return clock;
    }
    if (clock->frame + 120 < g_frame) {
      *link = clock->next;
      free(clock);
    } else {
      link = &clock->next;
    }
  }
  if (!available) {
    available = calloc(1, sizeof *available);
    if (available) {
      available->next = g_script_overflow;
      g_script_overflow = available;
    }
  }
  if (available) {
    ScriptClock *next = available->next;
    memset(available, 0, sizeof *available);
    available->next = next;
    available->object = object;
    available->kind = kind;
    available->state = state;
    available->frame = g_frame;
    available->sampled_frame = UINT64_MAX;
  }
  return available;
}

static unsigned legacy_steps(void) {
  uint64_t previous = (g_clock.remainder + SECOND_NS - g_clock.elapsed_units % SECOND_NS) % SECOND_NS;
  return g_clock.counter_steps + (g_clock.remainder + 29 >= SECOND_NS) - (previous + 29 >= SECOND_NS);
}

static unsigned script_steps(ScriptClock *clock, unsigned limit) {
  if (clock->sampled_frame == g_frame)
    return clock->steps;
  clock->sampled_frame = g_frame;
  unsigned available = legacy_steps();
  unsigned steps = available > limit ? limit : available;
  clock->remainder = (int64_t)((g_clock.remainder + 29) % SECOND_NS);
  clock->steps = steps;
  return steps;
}

static int scripted_player(void *object, void (*callback)(void *)) {
  void *player = object;
  if ((uintptr_t)callback == g_camera_callback) {
    player = (void *)(uintptr_t)(uint32_t)read_int(object, 0x18);
    if (!player || (uintptr_t)(uint32_t)read_int(player, 0x08) != g_player_callback)
      return 0;
  }
  void *key = (void *)(uintptr_t)(uint32_t)read_int(player, 0x18);
  if (!key || (uintptr_t)(uint32_t)read_int(key, 0x08) != g_key_callback)
    return 0;
  int32_t mode = read_int(key, 0x84);
  if (mode < 2 || mode > 4)
    return 0;
  ScriptClock *clock = script_at(key, 1, mode);
  if (clock && clock->recorded_player != player) {
    debugPrintf("[native60] recorded player=%p key=%p mode=%ld: original 30 Hz physics/camera\n",
                player, key, (long)mode);
    clock->recorded_player = player;
    clock->traces = 0;
    clock->traced_action = 0;
  }
  int is_player = (uintptr_t)callback == g_player_callback;
  if (clock ? script_steps(clock, 1) : legacy_steps()) {
    g_recorded_calls++;
    uint8_t *buttons = key;
    uint8_t previous_buttons = buttons[0x74];
    if (clock && clock->has_buttons)
      buttons[0x74] = clock->previous_buttons;
    uintptr_t action = (uintptr_t)(uint32_t)read_int(player, 0x6c);
    int spray_edge = is_player && ((buttons[0x6d] & ~buttons[0x74]) & 4);
    if (is_player && clock && clock->traces < 128 &&
        (clock->traced_action != action || ((buttons[0x6d] & ~buttons[0x74]) & 4))) {
      void *camera = (void *)(uintptr_t)(uint32_t)read_int(player, 0x850);
      debugPrintf("[replay] player=%p area=%lu action=%lx motion=%ld key=%02x/%02x record=%lx target=%lx cans=%ld pos=%.2f,%.2f,%.2f camera=%ld\n",
                  player, (unsigned long)g_last_area, (unsigned long)(action - g_game_base),
                  (long)read_int(player, 0x78), buttons[0x6d], buttons[0x74],
                  (unsigned long)(uint32_t)read_int(key, 0x70),
                  (unsigned long)(uint32_t)read_int(player, 0x274), (long)read_int(player, 0x3c8),
                  (double)read_float(player, 0xb8), (double)read_float(player, 0xbc),
                  (double)read_float(player, 0xc0),
                  camera ? (long)read_int(camera, 0xc4) : 0L);
      clock->traced_action = action;
      clock->traces++;
    }
    int previous = g_scaled;
    g_scaled = 0;
    if (is_player) {
      jsr_pose60_begin(player);
    }
    jsr_collision60_begin(object);
    callback(object);
    if (spray_edge && clock && clock->traces < 128) {
      debugPrintf("[replay] spray outcome player=%p action=%lx motion=%ld target=%lx available=%ld graffiti=%ld\n",
                  player, (unsigned long)((uintptr_t)(uint32_t)read_int(player, 0x6c) - g_game_base),
                  (long)read_int(player, 0x78), (unsigned long)(uint32_t)read_int(player, 0x274),
                  (long)read_int(player, 0x28c), (long)read_int(player, 0x294));
      clock->traces++;
    }
    jsr_collision60_end();
    if (is_player) {
      jsr_pose60_end();
    }
    g_scaled = previous;
    if (clock && clock->has_buttons && buttons[0x74] == clock->previous_buttons)
      buttons[0x74] = previous_buttons;
  } else {
    if (!jsr_collision60_hold(object) && !(g_collision_hold_failures++ % 600))
      debugPrintf("[native60] recorded collision renewal unavailable: actor=%p camera=%d; physics held to shared script tick\n", object, !is_player);
    g_recorded_holds++;
  }
  jsr_pose60_phase(player, (float)((double)((g_clock.remainder + 29) % SECOND_NS) / (double)SECOND_NS));
  return 1;
}

static void scripted_key(void *object, void (*callback)(void *), int32_t mode) {
  ScriptClock *clock = script_at(object, 1, mode);
  if (clock ? script_steps(clock, 1) : legacy_steps()) {
    callback(object);
    if (clock) {
      clock->previous_buttons = ((uint8_t *)object)[0x74];
      clock->has_buttons = 1;
    }
  } else {
    uint8_t *key = object;
    key[0x74] = key[0x6d];
  }
}

static void practice_step(void *object, void (*callback)(void *)) {
  int32_t state = read_int(object, 0x6c);
  ScriptClock *clock = script_at(object, 2, state);
  if (state != 2 && state != 4) {
    callback(object);
    return;
  }
  int32_t previous = read_int(object, 0x7c);
  unsigned steps = clock ? script_steps(clock, 3) : legacy_steps();
  int32_t prepared = previous;
  if (previous >= 0 && previous <= 90) {
    prepared = previous + (int32_t)steps - 1;
    write_int(object, 0x7c, prepared);
  }
  callback(object);
  if (read_int(object, 0x6c) == state && read_int(object, 0x7c) == prepared &&
      prepared != previous)
    write_int(object, 0x7c, previous);
}

void jsr_n60_dispatch(void *object, void (*callback)(void *)) {
  if (g_recorded_world_step) {
    int player = (uintptr_t)callback == g_player_callback;
    int actor = player || (uintptr_t)callback == g_camera_callback;
    int previous = g_scaled;
    g_scaled = 0;
    if (player)
      jsr_pose60_begin(object);
    if (actor)
      jsr_collision60_begin(object);
    callback(object);
    if (actor)
      jsr_collision60_end();
    if (player)
      jsr_pose60_end();
    g_scaled = previous;
    return;
  }
  if ((uintptr_t)callback == g_player_callback || (uintptr_t)callback == g_camera_callback)
    jsr_collision60_prepare(object);
  if (!g_active) {
    if ((uintptr_t)callback == g_player_callback || (uintptr_t)callback == g_camera_callback)
      jsr_collision60_begin(object);
    callback(object);
    if ((uintptr_t)callback == g_player_callback || (uintptr_t)callback == g_camera_callback)
      jsr_collision60_end();
    return;
  }
  uintptr_t address = (uintptr_t)callback;
  for (unsigned index = 0; index < sizeof g_graffiti_profiles / sizeof g_graffiti_profiles[0]; index++) {
    if (address == g_game_base + g_graffiti_profiles[index].offset + 1) {
      if (legacy_steps()) {
        int previous = g_scaled;
        g_scaled = 0;
        callback(object);
        g_scaled = previous;
      }
      return;
    }
  }
  if (address == g_demo_callback || address == g_radio_callback ||
      address == g_event_manager_callback || address == g_event_telop_callback) {
    if (legacy_steps()) {
      int previous = g_scaled;
      g_scaled = 0;
      callback(object);
      g_scaled = previous;
    }
    return;
  }
  if (address == g_lesson_callback) {
    int state;
    int16_t narrow_state;
    memcpy(&narrow_state, (uint8_t *)object + 0x6c, sizeof narrow_state);
    state = narrow_state;
    if (state == 2) {
      int32_t remaining = read_int(object, 0x70);
      if (remaining >= 0 && remaining < INT_MAX)
        write_int(object, 0x70, remaining + 1 - (int32_t)legacy_steps());
    }
    int previous = g_scaled;
    g_scaled = 1;
    callback(object);
    g_scaled = previous;
    return;
  }
  if (address == g_scene_callback) {
    int32_t state = read_int(object, 0x6c);
    if (state == 2 || state == 3) {
      float frame = read_float(object, 0x70);
      if (isfinite(frame))
        write_float(object, 0x70, frame + g_clock.scale - 1.0f);
    }
    int previous = g_scaled;
    g_scaled = 0;
    callback(object);
    g_scaled = previous;
    return;
  }
  if ((address == g_player_callback || address == g_camera_callback) && scripted_player(object, callback))
    return;
  if (address == g_player_callback) {
    jsr_pose60_forget(object);
  }
  if (address == g_key_callback) {
    int32_t mode = read_int(object, 0x84);
    if (mode >= 2 && mode <= 4) {
      int previous = g_scaled;
      g_scaled = 0;
      scripted_key(object, callback, mode);
      g_scaled = previous;
      return;
    }
  }
  if (address == g_practice_callback) {
    int previous = g_scaled;
    g_scaled = 1;
    practice_step(object, callback);
    g_scaled = previous;
    return;
  }
  if (address != g_player_callback && address != g_camera_callback) {
    int previous = g_scaled;
    g_scaled = 1;
    if (!jsr_event60_dispatch(object, callback, g_clock.scale, legacy_steps()))
      callback(object);
    g_scaled = previous;
    g_legacy_calls++;
    return;
  }
  int previous_scaled = g_scaled;
  g_scaled = 1;
  if (address == g_player_callback) {
    Player *player = player_at(object, 1);
    if (player) {
      int32_t previous[PLAYER_TIMERS], prepared[PLAYER_TIMERS];
      if (player->frame + 1 < g_frame) {
        memset(player->timers, 0, sizeof player->timers);
        jsr_engine_countdown_reset(&player->fall_clock);
      }
      player->frame = g_frame;
      player->vertical_correction = 0.0f;
      player->horizontal_correction = 0.0f;
      prepare_timers(player, previous, prepared);
      float timer = read_float(object, 0x870);
      float prepared_timer = timer;
      if (isfinite(timer) && timer > 0.0f) {
        prepared_timer = timer + 1.0f - g_clock.scale;
        write_float(object, 0x870, prepared_timer);
      }
      jsr_collision60_begin(object);
      callback(object);
      jsr_collision60_end();
      if (prepared_timer != timer && read_float(object, 0x870) == prepared_timer)
        write_float(object, 0x870, timer);
      finish_timers(player, previous, prepared);
    } else {
      if (legacy_steps()) {
        g_scaled = 0;
        jsr_collision60_begin(object);
        callback(object);
        jsr_collision60_end();
      } else {
        jsr_collision60_hold(object);
      }
    }
  } else {
    jsr_collision60_begin(object);
    callback(object);
    jsr_collision60_end();
  }
  g_scaled = previous_scaled;
  g_fast_calls++;
}

static void hooked_fall(void *object) {
  if (!g_scaled) {
    jsr_n60_original_fall(object);
    return;
  }
  float rise = read_float(object, 0xe8), fall = read_float(object, 0xec);
  float horizontal = read_float(object, 0xdc);
  void *previous_falling = g_falling;
  float previous_gravity = g_fall_gravity;
  g_falling = object;
  g_fall_gravity = fall;
  write_float(object, 0xe8, rise * g_clock.scale);
  write_float(object, 0xec, fall * g_clock.scale);
  jsr_n60_original_fall(object);
  if (read_float(object, 0xe8) == rise * g_clock.scale)
    write_float(object, 0xe8, rise);
  if (read_float(object, 0xec) == fall * g_clock.scale)
    write_float(object, 0xec, fall);
  float current = read_float(object, 0xdc);
  if (isfinite(horizontal) && isfinite(current)) {
    write_float(object, 0xdc, horizontal + (current - horizontal) * g_clock.scale);
    Player *player = player_at(object, 0);
    if (player)
      player->horizontal_correction += (current - horizontal) *
          (0.5f * g_clock.scale * (1.0f - g_clock.scale));
  }
  g_falling = previous_falling;
  g_fall_gravity = previous_gravity;
}

void jsr_n60_fall_block(void *object) {
  Player *player = g_scaled ? player_at(object, 0) : NULL;
  JsrEngineClock original = {.scale = 1.0f, .elapsed_units = SECOND_NS};
  JsrEngineCountdown unused = {0};
  if (!player) {
    jsr_engine_fall_block(&original, &unused, object);
    return;
  }
  int32_t remaining = read_int(object, 0xf0);
  if (remaining != player->fall_expected)
    jsr_engine_countdown_reset(&player->fall_clock);
  float stored = read_float(object, 0xec);
  if (g_falling == object)
    write_float(object, 0xec, g_fall_gravity);
  float displacement = jsr_engine_fall_block(&g_clock, &player->fall_clock, object);
  player->vertical_correction = displacement - read_float(object, 0xe4) * g_clock.scale;
  player->fall_expected = read_int(object, 0xf0);
  write_float(object, 0xec, stored);
}

typedef void (*Collision)(void *, void *);

static void collision_step(void *object, void *argument, Collision original) {
  Player *player = g_active && g_scaled ? player_at(object, 0) : NULL;
  if (!player || player->frame != g_frame || g_collision_depth) {
    original(object, argument);
    return;
  }
  static const unsigned offsets[] = {
      0xdc, 0xe4, 0x11c, 0x82c, 0x830, 0x834, 0x838, 0x83c, 0x840,
      0x844, 0x848, 0x84c,
  };
  float previous[sizeof offsets / sizeof offsets[0]];
  float scaled[sizeof offsets / sizeof offsets[0]];
  for (unsigned index = 0; index < sizeof offsets / sizeof offsets[0]; index++) {
    previous[index] = read_float(object, offsets[index]);
    scaled[index] = previous[index] * g_clock.scale;
    if (offsets[index] == 0xe4)
      scaled[index] += player->vertical_correction;
    if (offsets[index] == 0xdc)
      scaled[index] += player->horizontal_correction;
    write_float(object, offsets[index], scaled[index]);
  }
  g_collision_depth++;
  original(object, argument);
  g_collision_depth--;
  for (unsigned index = 0; index < sizeof offsets / sizeof offsets[0]; index++) {
    float current = read_float(object, offsets[index]);
    if (!memcmp(&current, &scaled[index], sizeof current))
      write_float(object, offsets[index], previous[index]);
  }
  g_collision_calls++;
}

static void original_jump(void *object, void *argument) {
  (void)argument;
  jsr_n60_original_jump(object);
}

static void original_grind(void *object, void *argument) {
  jsr_n60_original_grind(object, argument);
}

static void original_wall(void *object, void *argument) {
  (void)argument;
  jsr_n60_original_wall(object);
}

static void hooked_ride(void *object, void *argument) {
  collision_step(object, argument, jsr_n60_original_ride);
}

static void hooked_jump(void *object) {
  collision_step(object, NULL, original_jump);
}

static void hooked_grind(void *object, void *argument) {
  collision_step(object, argument, original_grind);
}

static void hooked_wall(void *object) {
  collision_step(object, NULL, original_wall);
}

static void hooked_stage(void) {
  if (!g_active) {
    jsr_n60_original_stage();
    return;
  }
  for (unsigned step = 0; step < legacy_steps(); step++)
    jsr_n60_original_stage();
}

static void hooked_exec(void) {
  void *root = g_active && g_action_get ? g_action_get(106) : NULL;
  void *key = root && (uintptr_t)(uint32_t)read_int(root, 0x08) == g_player_callback ?
              (void *)(uintptr_t)(uint32_t)read_int(root, 0x18) : NULL;
  int mode = key && (uintptr_t)(uint32_t)read_int(key, 0x08) == g_key_callback ? read_int(key, 0x84) : 0;
  int recorded = mode >= 2 && mode <= 4;
  if (recorded != g_recorded_world_active) {
    debugPrintf("[native60] recorded world: %s (root key mode=%d); visual poses remain native 60 Hz\n",
                recorded ? "original 30 Hz action/collision domain" : "live engine-time adapters", mode);
    jsr_pose60_reset();
    g_recorded_world_active = recorded;
    g_recorded_world_ticks = 0;
  }
  if (!recorded || legacy_steps()) {
    if (recorded && !(g_recorded_world_ticks++ % 30))
      debugPrintf("[replay-world] tick=%lu mode=%d player=%p frame=%ld cans=%ld pos=%.2f,%.2f,%.2f\n",
                  g_recorded_world_ticks, mode, root, (long)read_int(root, 0x98),
                  (long)read_int(root, 0x3c8), (double)read_float(root, 0xb8),
                  (double)read_float(root, 0xbc), (double)read_float(root, 0xc0));
    jsr_pose60_tick();
    jsr_collision60_tick();
    g_recorded_world_step = recorded;
    jsr_original_exec();
    g_recorded_world_step = 0;
  }
  if (recorded)
    jsr_pose60_present((float)((double)((g_clock.remainder + 29) % SECOND_NS) / (double)SECOND_NS));
  if (g_active && !(g_frame % 600))
    debugPrintf("[native60] frame=%llu scale=%.4f fast=%lu legacy=%lu motion=%lu collision=%lu recorded=%lu held=%lu\n",
                (unsigned long long)g_frame, (double)g_clock.scale, g_fast_calls,
                g_legacy_calls, g_motion_calls, g_collision_calls, g_recorded_calls, g_recorded_holds);
}

static uint8_t *staged(so_module *module, uintptr_t offset, size_t length) {
  if (module->state != SO_STAGED || offset > module->load_size ||
      length > module->load_size - offset)
    return NULL;
  return (uint8_t *)module->load_base + offset;
}

int jsr_native60_install(so_module *module) {
  g_requested = rt_config_bool("display", "native60");
  g_installed = 0;
  if (!g_requested)
    return 0;
  NativeHook hooks[] = {
      {"jtActionExec", 0x1afb84, {0x2d,0xe9,0xf8,0x4f,0xaf,0x4c,0xa4,0xf0,0xf9,0xfa}, 10, hooked_exec, &jsr_exec_tail},
      {"jtMotionNextFrame", 0x1ae354, {0xd1,0xed,0x00,0x7a,0x07,0xee,0x10,0x2a}, 8, hooked_motion, &jsr_n60_motion_tail},
      {"jtMotionNextFrameEx", 0x1ae414, {0xd1,0xed,0x00,0x7a,0x07,0xee,0x10,0x2a}, 8, hooked_motion_ex, &jsr_n60_motion_ex_tail},
      {"jtAngleSupple", 0x1a48e4, {0x03,0x04,0x09,0x04,0x1b,0x0c,0x08,0x0c}, 8, hooked_angle, &jsr_n60_angle_tail},
      {"jtFloatSupple", 0x1a492c, {0x06,0xee,0x10,0x0a,0x07,0xee,0x90,0x1a}, 8, hooked_scalar, &jsr_n60_scalar_tail},
      {"ride speed", 0x1e9e94, {0x70,0xb5,0x2d,0xed,0x02,0x8b,0x04,0x46}, 8, hooked_speed, &jsr_n60_speed_tail},
      {"turn ratio", 0x1d67e0, {0x07,0xee,0x90,0x2a,0xb7,0xee,0x00,0x7a}, 8, hooked_turn, &jsr_n60_turn_tail},
      {"ride collision", 0x1dcfac, {0x2d,0xe9,0xf0,0x41,0x04,0x46,0x90,0xb0}, 8, hooked_ride, &jsr_n60_ride_tail},
      {"jump collision", 0x1dcb28, {0x2d,0xe9,0xf0,0x41,0xd0,0xf8,0x34,0x32}, 8, hooked_jump, &jsr_n60_jump_tail},
      {"grind collision", 0x1dc85c, {0x2d,0xe9,0xf0,0x43,0x04,0x46,0x8f,0xb0}, 8, hooked_grind, &jsr_n60_grind_tail},
      {"wall collision", 0x1dc674, {0xf0,0xb5,0x04,0x46,0x8f,0xb0,0xd0,0xf8,0x8c,0x50}, 10, hooked_wall, &jsr_n60_wall_tail},
      {"fall", 0x1e6034, {0x10,0xb5,0xd0,0xf8,0x04,0x32,0x04,0x46}, 8, hooked_fall, &jsr_n60_fall_tail},
      {"stage clock", 0x1c4508, {0xf0,0xb5,0xb8,0x4c,0xb8,0x4b,0x87,0xb0}, 8, hooked_stage, &jsr_n60_stage_tail},
      {"dispatch first", 0x1afb18, {0x20,0x46,0xc8,0x47,0x23,0x68,0x03,0xf0,0x01,0x02}, 10, jsr_n60_dispatch_first, &jsr_n60_dispatch_first_tail},
      {"dispatch second", 0x1afb56, {0x20,0x46,0xc8,0x47,0x23,0x68,0x03,0xf0,0x01,0x02}, 10, jsr_n60_dispatch_second, &jsr_n60_dispatch_second_tail},
      {"fall step", 0x1e604e, {0x90,0xed,0x39,0x7a,0xd0,0xed,0x3b,0x7a,0x77,0xee}, 10, jsr_n60_fall_block_entry, &jsr_n60_fall_block_tail},
  };
  uintptr_t base = (uintptr_t)module->load_virtbase;
  if (module->state != SO_STAGED ||
      so_try_find_addr_rx(module, "exGamePlayer") != (base + 0x1d71c8 + 1) ||
      so_try_find_addr_rx(module, "jtMotionNextFrame") != (base + 0x1ae354 + 1)) {
    debugPrintf("[native60] unsupported APK profile; falling back to original timing\n");
    return 0;
  }
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uint8_t *alias = staged(module, hooks[index].offset, hooks[index].length);
    if (!alias || memcmp(alias, hooks[index].prefix, hooks[index].length)) {
      debugPrintf("[native60] signature mismatch: %s; no hooks installed\n", hooks[index].name);
      return 0;
    }
  }
  static const uint8_t key_prefix[] = {0x70,0xb5,0x90,0xf8,0x6d,0x20,0xd0,0xf8,0x84,0x30};
  static const uint8_t practice_prefix[] = {0xf8,0xb5,0x78,0x4e,0x05,0x6f,0x04,0x46,0x7e,0x44};
  uint8_t *key_alias = staged(module, 0x1e7d38, sizeof key_prefix);
  uint8_t *practice_alias = staged(module, 0x1ca1b8, sizeof practice_prefix);
  if (!key_alias || !practice_alias || memcmp(key_alias, key_prefix, sizeof key_prefix) ||
      memcmp(practice_alias, practice_prefix, sizeof practice_prefix)) {
    debugPrintf("[native60] unsupported scripted-input/practice profile; no hooks installed\n");
    return 0;
  }
  static const struct {
    uintptr_t offset;
    uint8_t prefix[8];
  } script_profiles[] = {
      {0x1af658, {0x40,0xf6,0x05,0x23,0x98,0x42,0x05,0xd8}},
      {0x185938, {0xf0,0xb5,0xfe,0x4c,0xfe,0x4b,0xab,0xb0}},
      {0x1867fc, {0x10,0xb5,0xc3,0x6e,0x04,0x46,0x02,0x2b}},
      {0x1ecec0, {0x10,0xb5,0xc3,0x6e,0x04,0x46,0x07,0x2b}},
      {0x1eed9c, {0x10,0xb5,0xb0,0xf9,0x6c,0x30,0x04,0x46}},
      {0x167934, {0x2d,0xe9,0xf0,0x4f,0xdf,0xf8,0x68,0x65}},
      {0x166ab4, {0x10,0xb5,0xc3,0x6e,0x04,0x46,0x02,0x2b}},
  };
  for (unsigned index = 0; index < sizeof script_profiles / sizeof script_profiles[0]; index++) {
    uint8_t *alias = staged(module, script_profiles[index].offset, 8);
    if (!alias || memcmp(alias, script_profiles[index].prefix, 8)) {
      debugPrintf("[native60] unsupported demo/scene/radio profile; no hooks installed\n");
      return 0;
    }
  }
  for (unsigned index = 0; index < sizeof g_graffiti_profiles / sizeof g_graffiti_profiles[0]; index++) {
    uint8_t *alias = staged(module, g_graffiti_profiles[index].offset, 8);
    if (!alias || memcmp(alias, g_graffiti_profiles[index].prefix, 8)) {
      debugPrintf("[native60] unsupported graffiti action profile; no hooks installed\n");
      return 0;
    }
  }
  const uintptr_t literal_offsets[] = {0x1afe48, 0x1c47ec, 0x1c47f0};
  uint32_t *literal_values[] = {(uint32_t *)&jsr_exec_literal,
                               &jsr_n60_stage_literals[0], &jsr_n60_stage_literals[1]};
  for (unsigned index = 0; index < 3; index++) {
    uint8_t *alias = staged(module, literal_offsets[index], sizeof(uint32_t));
    if (!alias)
      return 0;
    memcpy(literal_values[index], alias, sizeof(uint32_t));
  }
  jsr_timer_count = so_try_find_addr_rx(module, "syTmrGetCount");
  g_area = (uint32_t *)so_try_find_addr_rx(module, "u32StageArea");
  if (!jsr_timer_count || !g_area)
    return 0;
  if (!jsr_event60_validate(module))
    return 0;
  if (!jsr_collision60_install(module)) {
    debugPrintf("[native60] unsupported collision registration profile; original timing retained\n");
    return 0;
  }
  jsr_event60_install(module);
  g_game_base = base;
  g_action_get = (void *)(base + 0x1af659);
  g_player_callback = base + 0x1d71c8 + 1;
  g_camera_callback = base + 0x1d90b4 + 1;
  g_key_callback = base + 0x1e7d38 + 1;
  g_practice_callback = base + 0x1ca1b8 + 1;
  g_demo_callback = base + 0x185938 + 1;
  g_scene_callback = base + 0x1867fc + 1;
  g_radio_callback = base + 0x1ecec0 + 1;
  g_lesson_callback = base + 0x1eed9c + 1;
  g_event_manager_callback = base + 0x167934 + 1;
  g_event_telop_callback = base + 0x166ab4 + 1;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    NativeHook *hook = &hooks[index];
    uint8_t *alias = staged(module, hook->offset, hook->length);
    uint16_t instruction[] = {0xf8df, 0xf000};
    uint32_t target = (uint32_t)(uintptr_t)hook->replacement;
    unsigned literal = (hook->offset & 2) ? 6 : 4;
    if (literal == 6)
      instruction[1] |= 4;
    memset(alias, 0, hook->length);
    memcpy(alias, instruction, sizeof instruction);
    memcpy(alias + literal, &target, sizeof target);
    *hook->tail = (base + hook->offset + hook->length) | 1u;
  }
  jsr_n60_fall_block_tail = base + 0x1e6072 + 1;
  g_installed = 1;
  if (!jsr_pose60_install(module))
    debugPrintf("[pose60] unsupported animation profile; original recorded poses retained\n");
  jsr_native60_reset();
  debugPrintf("[native60] experimental engine-time adapters installed; interpolation OFF\n");
  debugPrintf("[native60] all action callbacks per present; stage clock at 30 Hz; unmapped per-tick systems remain experimental\n");
  debugPrintf("[native60] recorded keys/players/cameras, demo commands and radio share global 30 Hz ticks; scene evaluation and live input per present\n");
  return 1;
}

int jsr_native60_begin(void) {
  int locked = g_nat.isLocked30Fps && g_nat.isLocked30Fps(g_jni_env, g_lib_cls);
  int requested = rt_config_int("display", "frame_rate") == 1 ? 30 : 60;
  int active = g_installed && locked && requested == 60;
  unsigned hz = locked && !active ? 30u : (unsigned)requested;
  uint64_t now = armTicksToNs(armGetSystemTick());
  if (g_present_hz != hz || !g_deadline || now > g_deadline + SECOND_NS / 4) {
    g_deadline = now;
    g_present_remainder = 0;
    g_present_hz = hz;
  }
  if (now < g_deadline) {
    svcSleepThread((s64)(g_deadline - now));
    now = armTicksToNs(armGetSystemTick());
  }
  g_present_remainder += SECOND_NS;
  g_deadline += g_present_remainder / hz;
  g_present_remainder %= hz;
  if (g_active != active || (g_area && g_last_area != *g_area)) {
    while (g_player_overflow) {
      Player *player = g_player_overflow;
      g_player_overflow = player->next;
      free(player);
    }
    while (g_script_overflow) {
      ScriptClock *clock = g_script_overflow;
      g_script_overflow = clock->next;
      free(clock);
    }
    jsr_audio_catchup(0);
    jsr_collision60_reset();
    jsr_pose60_reset();
    jsr_engine_clock_reset(&g_clock);
    memset(g_players, 0, sizeof g_players);
    memset(g_scripts, 0, sizeof g_scripts);
    if (g_area)
      g_last_area = *g_area;
    debugPrintf("[native60] mode: %s\n", active ? "engine-time experiment" : "original timing");
  }
  g_active = active;
  int starting = active && !g_clock.started;
  jsr_engine_clock_begin(&g_clock, now, !active);
  if (starting) {
    g_clock.elapsed_units = SECOND_NS;
    g_clock.counter_steps = 1;
    g_clock.scale = 1.0f;
    debugPrintf("[native60] initial logic tick: input/player/camera/stage primed before collision-pool renewal\n");
  }
  if (g_clock.pending_ns)
    jsr_audio_catchup(1);
  g_frame++;
  return 1;
}

void jsr_native60_run(void (*step)(void *, void *), void *env, void *cls) {
  JsrEngineClock saved = g_clock;
  unsigned passes = g_active ? (unsigned)((saved.elapsed_units + SECOND_NS - 30) / SECOND_NS) : 1;
  if (passes < 2) {
    step(env, cls);
    if (!g_clock.pending_ns)
      jsr_audio_catchup(0);
    return;
  }
  if (passes > 3)
    passes = 3;
  uint32_t area = g_area ? *g_area : 0;
  uint64_t remainder = (saved.remainder + SECOND_NS - saved.elapsed_units % SECOND_NS) % SECOND_NS;
  for (unsigned pass = 0; pass < passes; pass++) {
    if (pass && (!g_active || rt_exit_requested() || !rt_focused() || (g_area && *g_area != area) ||
                 (g_nat.isLocked30Fps && !g_nat.isLocked30Fps(g_jni_env, g_lib_cls))))
      break;
    uint64_t units = saved.elapsed_units * (pass + 1) / passes - saved.elapsed_units * pass / passes;
    g_clock.elapsed_units = units;
    g_clock.scale = (float)((double)units / (double)SECOND_NS);
    g_clock.counter_steps = (unsigned)((remainder + units) / SECOND_NS);
    remainder = (remainder + units) % SECOND_NS;
    g_clock.remainder = remainder;
    if (pass)
      g_frame++;
    step(env, cls);
  }
  g_clock = saved;
  if (!g_clock.pending_ns)
    jsr_audio_catchup(0);
}
