#include <math.h>
#include <string.h>

#include "jsr_engine_clock.h"

void jsr_engine_clock_reset(JsrEngineClock *clock) {
  memset(clock, 0, sizeof *clock);
}

void jsr_engine_clock_begin(JsrEngineClock *clock, uint64_t now, int paused) {
  clock->scale = 0.0f;
  clock->counter_steps = 0;
  clock->elapsed_units = 0;
  if (paused) {
    jsr_engine_clock_reset(clock);
    return;
  }
  if (!clock->started || now < clock->last_ns) {
    clock->started = 1;
    clock->last_ns = now;
    clock->remainder = 0;
    clock->pending_ns = 0;
    return;
  }
  uint64_t elapsed = now - clock->last_ns;
  clock->last_ns = now;
  clock->pending_ns = elapsed > UINT64_MAX - clock->pending_ns ? UINT64_MAX : clock->pending_ns + elapsed;
  elapsed = clock->pending_ns > 100000000ull ? 100000000ull : clock->pending_ns;
  clock->pending_ns -= elapsed;
  clock->scale = (float)((double)elapsed * 30.0 / 1000000000.0);
  clock->elapsed_units = elapsed * 30u;
  clock->remainder += clock->elapsed_units;
  clock->counter_steps = clock->remainder / 1000000000ull;
  clock->remainder %= 1000000000ull;
}

float jsr_engine_clock_scale(const JsrEngineClock *clock, float step) {
  if (!isfinite(step) || clock->scale == 1.0f)
    return step;
  return step * clock->scale;
}

void jsr_engine_countdown_reset(JsrEngineCountdown *countdown) {
  countdown->remainder = 0;
}

int jsr_engine_countdown_advance(const JsrEngineClock *clock,
                                 JsrEngineCountdown *countdown, int32_t *remaining) {
  if (*remaining <= 0) {
    jsr_engine_countdown_reset(countdown);
    return 0;
  }
  countdown->remainder += clock->elapsed_units;
  uint64_t steps = countdown->remainder / 1000000000ull;
  countdown->remainder %= 1000000000ull;
  if (steps >= (uint32_t)*remaining) {
    *remaining = 0;
    jsr_engine_countdown_reset(countdown);
    return 1;
  }
  *remaining -= (int32_t)steps;
  return 0;
}

int jsr_engine_motion_next(const JsrEngineClock *clock, JsrMotionNext original,
                           const void *motion, float *frame, float step, int flags) {
  return original(motion, frame, jsr_engine_clock_scale(clock, step), flags);
}

int jsr_engine_motion_next_ex(const JsrEngineClock *clock, JsrMotionNextEx original,
                              const void *motion, float *frame, float step,
                              float start, float end, int flags) {
  return original(motion, frame, jsr_engine_clock_scale(clock, step), start, end, flags);
}
