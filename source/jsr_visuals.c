#include <math.h>
#include <stdint.h>
#include <string.h>

#include "jsr_visuals.h"
#include "rt_cfg.h"
#include "util.h"

uintptr_t jsr_visual_circle_tails[3];
extern void jsr_visual_original_circle0(float, uint32_t);
extern void jsr_visual_original_circle1(float, uint32_t);
extern void jsr_visual_original_circle2(float, uint32_t);
static float g_draw_scale = 1.0f;

static void circle0(float radius, uint32_t distance) {
  jsr_visual_original_circle0(radius * g_draw_scale, distance);
}

static void circle1(float radius, uint32_t distance) {
  jsr_visual_original_circle1(radius * g_draw_scale, distance);
}

static void circle2(float radius, uint32_t distance) {
  jsr_visual_original_circle2(radius * g_draw_scale, distance);
}

int jsr_visuals_install(so_module *module) {
  static const char *names[] = {
      "TS_MKMN_CLIP_DIST", "TS_DKMN_CLIP_DIST", "TS_ROAD_CLIP_DIST", "TS_BILL_CLIP_DIST",
      "DT_MKMN_CLIP_DIST", "DT_DKMN_CLIP_DIST", "DT_ROAD_CLIP_DIST", "DT_BILL_CLIP_DIST",
      "TS_MKMN_MIP_DIST", "TS_DKMN_MIP_DIST", "TS_ROAD_MIP_DIST", "TS_BILL_MIP_DIST",
      "DT_MKMN_MIP_DIST", "DT_DKMN_MIP_DIST", "DT_ROAD_MIP_DIST", "DT_BILL_MIP_DIST", "DT_MOD_MIP_DIST",
  };
  static const uintptr_t offsets[] = {0x1c4e54, 0x1c4e78, 0x1c4e9c};
  static const uint8_t prefix[] = {0x07,0xee,0x90,0x0a,0x67,0xee,0xa7,0x7a};
  void (*replacements[])(float, uint32_t) = {circle0, circle1, circle2};
  uint8_t *variables[sizeof names / sizeof names[0]];
  float values[sizeof names / sizeof names[0]];
  float draw = rt_config_float("display", "draw_distance");
  float detail = rt_config_float("display", "detail_distance");
  if (module->state != SO_STAGED || !isfinite(draw) || !isfinite(detail) ||
      draw < 1.0f || draw > 2.5f || detail < 1.0f || detail > 2.0f)
    return 0;
  uintptr_t base = (uintptr_t)module->load_virtbase;
  for (unsigned index = 0; index < 3; index++) {
    if (offsets[index] > module->load_size || sizeof prefix > module->load_size - offsets[index] ||
        memcmp((uint8_t *)module->load_base + offsets[index], prefix, sizeof prefix))
      return 0;
  }
  for (unsigned index = 0; index < sizeof names / sizeof names[0]; index++) {
    uintptr_t address = so_try_find_addr_rx(module, names[index]);
    if (address < base || address - base > module->load_size ||
        sizeof(float) > module->load_size - (address - base))
      return 0;
    variables[index] = (uint8_t *)module->load_base + address - base;
    memcpy(&values[index], variables[index], sizeof(float));
    if (!isfinite(values[index]) || values[index] <= 0.0f)
      return 0;
  }
  for (unsigned index = 0; index < sizeof names / sizeof names[0]; index++) {
    values[index] *= index < 8 ? draw : detail;
    memcpy(variables[index], &values[index], sizeof(float));
  }
  g_draw_scale = draw;
  for (unsigned index = 0; index < 3; index++) {
    uint32_t stub[] = {0xf000f8df, (uint32_t)(uintptr_t)replacements[index]};
    memcpy((uint8_t *)module->load_base + offsets[index], stub, sizeof stub);
    jsr_visual_circle_tails[index] = (base + offsets[index] + sizeof stub) | 1u;
  }
  debugPrintf("[visuals] object visibility x%.2f, detailed models/textures x%.2f; 17 limits adjusted\n",
              (double)draw, (double)detail);
  return 1;
}
