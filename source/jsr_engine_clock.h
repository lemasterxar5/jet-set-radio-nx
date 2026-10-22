#ifndef JSR_ENGINE_CLOCK_H
#define JSR_ENGINE_CLOCK_H

#include <stdint.h>

typedef struct {
  uint64_t last_ns, remainder, elapsed_units, pending_ns;
  float scale;
  unsigned counter_steps;
  int started;
} JsrEngineClock;

typedef struct {
  uint64_t remainder;
} JsrEngineCountdown;

typedef int (*JsrMotionNext)(const void *, float *, float, int);
typedef int (*JsrMotionNextEx)(const void *, float *, float, float, float, int);

void jsr_engine_clock_reset(JsrEngineClock *clock);
void jsr_engine_clock_begin(JsrEngineClock *clock, uint64_t now, int paused);
float jsr_engine_clock_scale(const JsrEngineClock *clock, float step);
void jsr_engine_countdown_reset(JsrEngineCountdown *countdown);
int jsr_engine_countdown_advance(const JsrEngineClock *clock,
                                 JsrEngineCountdown *countdown, int32_t *remaining);
int jsr_engine_motion_next(const JsrEngineClock *clock, JsrMotionNext original,
                           const void *motion, float *frame, float step, int flags);
int jsr_engine_motion_next_ex(const JsrEngineClock *clock, JsrMotionNextEx original,
                              const void *motion, float *frame, float step,
                              float start, float end, int flags);

#endif
