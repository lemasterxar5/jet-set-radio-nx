#include <stdint.h>
#include <string.h>

#include "jsr_render_safety.h"
#include "util.h"

uintptr_t jsr_shadow_polygon_tail;
extern float *jsr_shadow_original_polygon(const int16_t *, void *, uint32_t, float *);
static unsigned long g_empty_batches;

static float *shadow_polygon(const int16_t *indices, void *vertices, uint32_t header, float *alpha) {
  if (!(header & 0x3fffu)) {
    if (g_empty_batches++ < 4)
      debugPrintf("[shadow] empty modifier polygon batch skipped: header=0x%08lx indices=%p\n",
                  (unsigned long)header, (const void *)indices);
    return alpha;
  }
  return jsr_shadow_original_polygon(indices, vertices, header, alpha);
}

int jsr_render_safety_install(so_module *module) {
  static const uint8_t prefix[] = {0x2d,0xe9,0xf0,0x4f,0x2d,0xed,0x02,0x8b};
  const uintptr_t offset = 0x257160;
  uintptr_t base = (uintptr_t)module->load_virtbase;
  if (module->state != SO_STAGED || offset > module->load_size ||
      sizeof prefix > module->load_size - offset ||
      so_try_find_addr_rx(module, "_Z15njCnkModPolygonPsP15NJS_CNK_MOD_BUFmPf") != base + offset + 1 ||
      memcmp((uint8_t *)module->load_base + offset, prefix, sizeof prefix))
    return 0;
  uint32_t stub[] = {0xf000f8df, (uint32_t)(uintptr_t)shadow_polygon};
  memcpy((uint8_t *)module->load_base + offset, stub, sizeof stub);
  jsr_shadow_polygon_tail = base + offset + sizeof stub + 1;
  g_empty_batches = 0;
  debugPrintf("[shadow] zero-triangle modifier guard installed; nonempty shadows unchanged\n");
  return 1;
}
