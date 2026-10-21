#include <math.h>
#include <stdint.h>
#include <string.h>
#include <switch.h>

#include "jsr.h"
#include "jsr_native60.h"
#include "jsr_timing.h"
#include "rt_cfg.h"
#include "util.h"

#define MATRIX_SLOTS 2048
#define MOTION_SLOTS 512
#define CAMERA_SLOTS 32
#define SECOND_NS 1000000000ull

typedef struct {
  uintptr_t first, second;
  uint32_t generation;
  int previous_valid;
  float previous[16], current[16];
} Sample;

typedef struct {
  float position[3], direction[3];
  int32_t roll;
} Camera;

typedef struct {
  Camera pose;
  int32_t field_of_view;
  float near_clip, far_clip;
} GameCamera;

_Static_assert(sizeof(Camera) == 28, "Ninja view camera ABI");
_Static_assert(sizeof(GameCamera) == 40, "Ninja camera and projection ABI");

uintptr_t jsr_exec_literal, jsr_draw_literal, jsr_view_literal;
uintptr_t jsr_exec_tail, jsr_draw_tail, jsr_matrix_tail;
uintptr_t jsr_motion_tail, jsr_camera_tail, jsr_view_tail;
uintptr_t jsr_timer_count;

extern void jsr_original_exec(void);
extern void jsr_original_draw(void);
extern void jsr_original_matrix(float *destination, const float *source);
extern int jsr_original_motion(void *object, const void *motion, float frame, int flags);
extern void jsr_original_camera(const GameCamera *camera);
extern void jsr_original_view(void *view);

static Sample g_matrices[MATRIX_SLOTS], g_motions[MOTION_SLOTS], g_cameras[CAMERA_SLOTS];
static unsigned g_matrix_slot, g_motion_slot, g_camera_slot;
static uint32_t g_generation;
static uint64_t g_last_ns, g_clock, g_deadline, g_present_remainder;
static unsigned g_present_hz;
static int g_installed, g_active, g_logic, g_executed, g_render, g_pose_depth, g_camera_depth;
static float g_alpha;
static float *g_camera_matrix, *g_view_matrix;
static float g_raw_camera[16], g_raw_view[16];
static int g_restore_camera;
static uint32_t *g_area;
static uint32_t g_last_area;
static unsigned long g_logic_count, g_draw_count, g_matrix_count, g_motion_count, g_camera_count;

static uint64_t now_ns(void) {
  return armTicksToNs(armGetSystemTick());
}

static void clear_history(void) {
  memset(g_matrices, 0, sizeof g_matrices);
  memset(g_motions, 0, sizeof g_motions);
  memset(g_cameras, 0, sizeof g_cameras);
  g_generation = 0;
}

void jsr_timing_reset(void) {
  jsr_native60_reset();
  clear_history();
  g_last_ns = g_clock = g_deadline = g_present_remainder = 0;
  g_active = g_render = g_pose_depth = g_camera_depth = 0;
  g_logic = 1;
  g_present_hz = 0;
}

static Sample *sample_at(Sample *samples, unsigned capacity, unsigned *slot,
                         uintptr_t first, uintptr_t second, const float *values, unsigned count) {
  if (*slot >= capacity)
    return NULL;
  Sample *sample = &samples[(*slot)++];
  if (sample->first != first || sample->second != second ||
      (sample->generation != g_generation && sample->generation + 1 != g_generation)) {
    memset(sample, 0, sizeof *sample);
    sample->first = first;
    sample->second = second;
    sample->generation = g_generation;
    memcpy(sample->current, values, count * sizeof(float));
  } else if (sample->generation != g_generation) {
    memcpy(sample->previous, sample->current, count * sizeof(float));
    memcpy(sample->current, values, count * sizeof(float));
    sample->previous_valid = 1;
    sample->generation = g_generation;
  }
  return sample;
}

static int finite_values(const float *values, unsigned count) {
  for (unsigned index = 0; index < count; index++)
    if (!isfinite(values[index]))
      return 0;
  return 1;
}

static float mix(float previous, float current) {
  return previous + (current - previous) * g_alpha;
}

static int matrix_mix(const Sample *sample, float output[16]) {
  if (!sample || !sample->previous_valid || !finite_values(sample->previous, 16) ||
      !finite_values(sample->current, 16))
    return 0;
  const float *previous = sample->previous, *current = sample->current;
  if (fabsf(previous[3]) > 0.001f || fabsf(previous[7]) > 0.001f ||
      fabsf(previous[11]) > 0.001f || fabsf(previous[15] - 1.0f) > 0.001f ||
      fabsf(current[3]) > 0.001f || fabsf(current[7]) > 0.001f ||
      fabsf(current[11]) > 0.001f || fabsf(current[15] - 1.0f) > 0.001f)
    return 0;
  float distance = 0.0f;
  for (unsigned index = 12; index < 15; index++) {
    float delta = current[index] - previous[index];
    distance += delta * delta;
  }
  if (distance > 250000.0f)
    return 0;
  for (unsigned index = 0; index < 16; index++)
    output[index] = mix(previous[index], current[index]);
  for (unsigned row = 0; row < 3; row++) {
    float old_length = 0.0f, new_length = 0.0f, mixed_length = 0.0f, dot = 0.0f;
    for (unsigned column = 0; column < 3; column++) {
      unsigned index = row * 4 + column;
      old_length += previous[index] * previous[index];
      new_length += current[index] * current[index];
      mixed_length += output[index] * output[index];
      dot += previous[index] * current[index];
    }
    if (dot <= 0.0f || mixed_length < 0.000001f)
      return 0;
    float scale = mix(sqrtf(old_length), sqrtf(new_length)) / sqrtf(mixed_length);
    for (unsigned column = 0; column < 3; column++)
      output[row * 4 + column] *= scale;
  }
  return finite_values(output, 16);
}

static void hooked_exec(void) {
  if (!g_active || (g_logic && !g_executed)) {
    g_executed = 1;
    g_logic_count++;
    jsr_original_exec();
  }
}

static void hooked_draw(void) {
  if (g_render) {
    g_render = 0;
    jsr_original_draw();
    g_render = 1;
    return;
  }
  if (!g_active) {
    jsr_original_draw();
    return;
  }
  g_matrix_slot = g_motion_slot = g_camera_slot = 0;
  g_restore_camera = 0;
  g_render = 1;
  jsr_original_draw();
  g_render = 0;
  if (g_restore_camera) {
    memcpy(g_camera_matrix, g_raw_camera, sizeof g_raw_camera);
    memcpy(g_view_matrix, g_raw_view, sizeof g_raw_view);
  }
  if (!(++g_draw_count % 600))
    debugPrintf("[timing] logic=%lu draw=%lu interpolated matrices=%lu motions=%lu cameras=%lu\n",
                g_logic_count, g_draw_count, g_matrix_count, g_motion_count, g_camera_count);
}

static void hooked_matrix(float *destination, const float *source) {
  if (!g_render || g_pose_depth || g_camera_depth || !source) {
    jsr_original_matrix(destination, source);
    return;
  }
  float output[16];
  Sample *sample = sample_at(g_matrices, MATRIX_SLOTS, &g_matrix_slot,
                             (uintptr_t)source, (uintptr_t)destination, source, 16);
  if (matrix_mix(sample, output)) {
    g_matrix_count++;
    jsr_original_matrix(destination, output);
  } else {
    jsr_original_matrix(destination, source);
  }
}

static int hooked_motion(void *object, const void *motion, float frame, int flags) {
  if (g_render && motion && isfinite(frame)) {
    Sample *sample = sample_at(g_motions, MOTION_SLOTS, &g_motion_slot,
                               (uintptr_t)object, (uintptr_t)motion, &frame, 1);
    if (sample && sample->previous_valid) {
      uint16_t length;
      memcpy(&length, (const uint8_t *)motion + 2, sizeof length);
      float delta = sample->current[0] - sample->previous[0];
      if (length) {
        if (delta < -(float)length * 0.5f)
          delta += length;
        if (delta > (float)length * 0.5f)
          delta -= length;
        if (fabsf(delta) <= 8.0f) {
          frame = sample->previous[0] + delta * g_alpha;
          if (frame < 0.0f)
            frame += length;
          if (frame >= length)
            frame -= length;
          g_motion_count++;
        }
      }
    }
  }
  g_pose_depth++;
  int result = jsr_original_motion(object, motion, frame, flags);
  g_pose_depth--;
  return result;
}

static int camera_mix(const void *key, const Camera *raw, Camera *output) {
  float values[7];
  memcpy(values, raw, 6 * sizeof(float));
  values[6] = (float)((uint32_t)raw->roll & 0xffffu);
  Sample *sample = sample_at(g_cameras, CAMERA_SLOTS, &g_camera_slot,
                             (uintptr_t)key, 0, values, 7);
  if (!sample || !sample->previous_valid || !finite_values(values, 6) ||
      !finite_values(sample->previous, 6))
    return 0;
  float distance = 0.0f, dot = 0.0f;
  for (unsigned index = 0; index < 3; index++) {
    float delta = sample->current[index] - sample->previous[index];
    distance += delta * delta;
    dot += sample->current[index + 3] * sample->previous[index + 3];
  }
  if (distance > 250000.0f || dot <= 0.0f)
    return 0;
  for (unsigned index = 0; index < 3; index++) {
    output->position[index] = mix(sample->previous[index], sample->current[index]);
    output->direction[index] = mix(sample->previous[index + 3], sample->current[index + 3]);
  }
  int32_t delta = (int32_t)sample->current[6] - (int32_t)sample->previous[6];
  if (delta > 32767)
    delta -= 65536;
  if (delta < -32768)
    delta += 65536;
  output->roll = ((int32_t)sample->previous[6] + (int32_t)((float)delta * g_alpha)) & 0xffff;
  g_camera_count++;
  return 1;
}

static void save_camera_matrices(void) {
  memcpy(g_raw_camera, g_camera_matrix, sizeof g_raw_camera);
  memcpy(g_raw_view, g_view_matrix, sizeof g_raw_view);
  g_restore_camera = 1;
}

static void restore_camera_matrices(void) {
  if (g_restore_camera) {
    memcpy(g_camera_matrix, g_raw_camera, sizeof g_raw_camera);
    memcpy(g_view_matrix, g_raw_view, sizeof g_raw_view);
  }
}

static void hooked_camera(const GameCamera *camera) {
  if (!g_render || !camera) {
    jsr_original_camera(camera);
    return;
  }
  GameCamera output = *camera;
  g_camera_depth++;
  restore_camera_matrices();
  jsr_original_camera(camera);
  save_camera_matrices();
  if (camera_mix(camera, &camera->pose, &output.pose))
    jsr_original_camera(&output);
  g_camera_depth--;
}

static void hooked_view(void *view) {
  if (!g_render || !view) {
    jsr_original_view(view);
    return;
  }
  Camera raw, output;
  float matrix[16];
  memcpy(&raw, (uint8_t *)view + 64, sizeof raw);
  g_camera_depth++;
  restore_camera_matrices();
  jsr_original_view(view);
  memcpy(matrix, view, sizeof matrix);
  save_camera_matrices();
  if (camera_mix(view, &raw, &output)) {
    memcpy((uint8_t *)view + 64, &output, sizeof output);
    jsr_original_view(view);
    memcpy((uint8_t *)view + 64, &raw, sizeof raw);
    memcpy(view, matrix, sizeof matrix);
  }
  g_camera_depth--;
}

typedef struct {
  const char *name;
  uint8_t prefix[10];
  unsigned length;
  void *replacement;
  uintptr_t *tail;
} Hook;

static uint8_t *staged_address(so_module *module, uintptr_t address, size_t length) {
  uintptr_t base = (uintptr_t)module->load_virtbase;
  if (module->state != SO_STAGED || address < base || address - base > module->load_size ||
      length > module->load_size - (address - base))
    return NULL;
  return (uint8_t *)module->load_base + (address - base);
}

int jsr_timing_install(so_module *module) {
  int native = jsr_native60_install(module);
  if (jsr_native60_requested())
    return native;
  Hook hooks[] = {
      {"jtActionExec", {0x2d,0xe9,0xf8,0x4f,0xaf,0x4c,0xa4,0xf0,0xf9,0xfa}, 10, hooked_exec, &jsr_exec_tail},
      {"jtActionDraw", {0x2d,0xe9,0xf8,0x43,0x3e,0x4c,0xa4,0xf0,0x97,0xfc}, 10, hooked_draw, &jsr_draw_tail},
      {"njMultiMatrix", {0xb0,0xb5,0x2d,0xed,0x10,0x8b,0x98,0xb0}, 8, hooked_matrix, &jsr_matrix_tail},
      {"jtMotionDraw", {0xf0,0xb5,0x2d,0xed,0x02,0x8b,0x08,0xee,0x10,0x2a}, 10, hooked_motion, &jsr_motion_tail},
      {"njSetCamera", {0x2d,0xe9,0xf0,0x41,0x2d,0xed,0x02,0x8b}, 8, hooked_camera, &jsr_camera_tail},
      {"njSetView", {0x38,0xb5,0x6a,0x49,0x04,0x46,0x2d,0xed,0x02,0x8b}, 10, hooked_view, &jsr_view_tail},
  };
  uintptr_t addresses[sizeof hooks / sizeof hooks[0]];
  uint8_t *aliases[sizeof hooks / sizeof hooks[0]];
  g_installed = 0;
  if (!rt_config_bool("display", "interpolate"))
    return 0;
  if (module->state != SO_STAGED)
    return 0;
  jsr_timer_count = so_try_find_addr_rx(module, "syTmrGetCount");
  g_camera_matrix = (float *)so_try_find_addr_rx(module, "_nj_camera_matrix_");
  g_view_matrix = (float *)so_try_find_addr_rx(module, "_nj_view_matrix_");
  g_area = (uint32_t *)so_try_find_addr_rx(module, "u32StageArea");
  if (!jsr_timer_count || !g_camera_matrix || !g_view_matrix || !g_area)
    return 0;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uintptr_t symbol = so_try_find_addr_rx(module, hooks[index].name);
    uintptr_t address = symbol & ~(uintptr_t)1;
    aliases[index] = staged_address(module, address, hooks[index].length);
    if (!(symbol & 1) || (address & 3) || !aliases[index] ||
        memcmp(aliases[index], hooks[index].prefix, hooks[index].length)) {
      debugPrintf("[timing] unsupported %s; interpolation disabled\n", hooks[index].name);
      return 0;
    }
    addresses[index] = address;
    *hooks[index].tail = (address + hooks[index].length) | 1u;
  }
  uint8_t *exec_literal = staged_address(module, addresses[0] + 0x2c4, sizeof(uint32_t));
  uint8_t *draw_literal = staged_address(module, addresses[1] + 0x100, sizeof(uint32_t));
  uint8_t *view_literal = staged_address(module, addresses[5] + 0x1ac, sizeof(uint32_t));
  if (!exec_literal || !draw_literal || !view_literal)
    return 0;
  memcpy(&jsr_exec_literal, exec_literal, sizeof(uint32_t));
  memcpy(&jsr_draw_literal, draw_literal, sizeof(uint32_t));
  memcpy(&jsr_view_literal, view_literal, sizeof(uint32_t));
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uint32_t stub[] = {0xf000f8dfu, (uint32_t)(uintptr_t)hooks[index].replacement};
    memcpy(aliases[index], stub, sizeof stub);
  }
  g_installed = 1;
  jsr_timing_reset();
  debugPrintf("[timing] installed 30 Hz logic / 60 Hz render interpolation (experimental)\n");
  return 1;
}

int jsr_timing_begin(void) {
  if (jsr_native60_requested())
    return jsr_native60_begin();
  int locked = g_nat.isLocked30Fps && g_nat.isLocked30Fps(g_jni_env, g_lib_cls);
  int requested = rt_config_int("display", "frame_rate") == 1 ? 30 : 60;
  int active = g_installed && requested == 60 && locked;
  unsigned hz = locked && !active ? 30u : (unsigned)requested;
  uint64_t now = now_ns();
  if (g_present_hz != hz || !g_deadline || now > g_deadline + SECOND_NS / 4) {
    g_deadline = now;
    g_present_remainder = 0;
    g_present_hz = hz;
  }
  if (now < g_deadline) {
    svcSleepThread((s64)(g_deadline - now));
    now = now_ns();
  }
  g_present_remainder += SECOND_NS;
  g_deadline += g_present_remainder / hz;
  g_present_remainder %= hz;
  if (active != g_active || (g_area && g_last_area != *g_area)) {
    clear_history();
    g_clock = SECOND_NS;
    g_last_ns = now;
    if (g_area)
      g_last_area = *g_area;
    debugPrintf("[timing] mode: %s\n", active ? "30 Hz logic, interpolated rendering" : "native timing");
  }
  g_active = active;
  g_executed = 0;
  g_logic = 1;
  g_alpha = 1.0f;
  if (g_active) {
    uint64_t elapsed = now - g_last_ns;
    if (elapsed > SECOND_NS / 10)
      elapsed = SECOND_NS / 10;
    g_clock += elapsed * 30u;
    g_logic = g_clock >= SECOND_NS;
    if (g_logic) {
      g_clock %= SECOND_NS;
      g_generation++;
    }
    g_alpha = (float)g_clock / (float)SECOND_NS;
  }
  g_last_ns = now;
  return g_logic;
}
