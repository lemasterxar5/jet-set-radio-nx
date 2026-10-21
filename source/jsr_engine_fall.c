#include <string.h>

#include "jsr_engine_fall.h"

float jsr_engine_fall_block(const JsrEngineClock *clock,
                           JsrEngineCountdown *countdown, void *player) {
  if (!clock->elapsed_units)
    return 0.0f;
  uint8_t *bytes = player;
  float velocity, gravity;
  int32_t remaining;
  memcpy(&velocity, bytes + 0xe4, sizeof velocity);
  memcpy(&gravity, bytes + 0xec, sizeof gravity);
  memcpy(&remaining, bytes + 0xf0, sizeof remaining);
  float scale = clock->scale;
  velocity -= gravity * scale;
  memcpy(bytes + 0xe4, &velocity, sizeof velocity);
  if (jsr_engine_countdown_advance(clock, countdown, &remaining)) {
    uint32_t cleared = 0;
    memcpy(bytes + 0x208, &cleared, sizeof cleared);
  }
  memcpy(bytes + 0xf0, &remaining, sizeof remaining);
  return velocity * scale + gravity * (0.5f * scale * (scale - 1.0f));
}
