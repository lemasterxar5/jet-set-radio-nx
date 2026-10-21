#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "rt_audout.h"
#include "jsr.h"
#include "util.h"

#define SOURCE_RATE 44100u
#define STREAM_COUNT 20
#define SOURCE_FRAMES RT_AUDOUT_FRAMES
#define SOURCE_SAMPLES (SOURCE_FRAMES * 2)

static JObj *g_array;
static int16_t *g_buf;
static int32_t g_mix[SOURCE_SAMPLES];
static int g_source_pos;
static int16_t g_previous[2], g_next[2];
static uint32_t g_phase, g_rate;
static int g_seeded, g_pump;
static volatile int g_running, g_paused;
static unsigned long g_blocks, g_invalid, g_active_mask;
static int g_peak, g_reported_sound;
static Mutex g_lock;
static void *(*g_sound_interface)(void);
static int (*g_game_disabled)(void), (*g_video_running)(void);
static const int *g_language;

void jsr_audio_bind(so_module *module) {
  g_sound_interface = (void *)so_try_find_addr_rx(module, "_ZN13AudioPlayback17GetSoundInterfaceEv");
  g_game_disabled = (void *)so_try_find_addr_rx(module, "_ZN12CApplication14IsGameDisabledEv");
  g_video_running = (void *)so_try_find_addr_rx(module, "_Z20IsVideoPlayerRunningv");
  g_language = (const int *)so_try_find_addr_rx(module, "androidLang");
}

static int16_t clamp_sample(int32_t value) {
  if (value > 32767)
    return 32767;
  if (value < -32768)
    return -32768;
  return (int16_t)value;
}

static void refill(void) {
  memset(g_mix, 0, sizeof g_mix);
  mutexLock(&g_lock);
  for (int stream = 0; stream < STREAM_COUNT; stream++) {
    memset(g_buf, 0, SOURCE_SAMPLES * sizeof *g_buf);
    int samples = g_nat.audioFillStreamBuffer(g_jni_env, g_lib_cls, stream,
                                             g_array, SOURCE_SAMPLES);
    if (samples < 0 || samples > SOURCE_SAMPLES) {
      if (g_invalid++ < 4)
        debugPrintf("[audio] stream %d returned invalid sample count %d\n", stream, samples);
      continue;
    }
    if (samples)
      g_active_mask |= 1u << stream;
    samples &= ~1;
    for (int sample = 0; sample < samples; sample++)
      g_mix[sample] += g_buf[sample];
  }
  mutexUnlock(&g_lock);
  g_source_pos = 0;
}

static void source_frame(int16_t frame[2]) {
  if (g_source_pos >= SOURCE_FRAMES)
    refill();
  frame[0] = clamp_sample(g_mix[g_source_pos * 2]);
  frame[1] = clamp_sample(g_mix[g_source_pos * 2 + 1]);
  g_source_pos++;
}

static void pump(int16_t *out, int frames, void *ud) {
  (void)ud;
  memset(out, 0, (size_t)frames * 2 * sizeof *out);
  if (!g_running || g_paused || !g_array || !g_nat.audioFillStreamBuffer)
    return;
  if (!g_seeded) {
    source_frame(g_previous);
    source_frame(g_next);
    g_seeded = 1;
  }
  for (int frame = 0; frame < frames; frame++) {
    for (int channel = 0; channel < 2; channel++) {
      int32_t value = (int32_t)(((int64_t)g_previous[channel] * (g_rate - g_phase) +
                                (int64_t)g_next[channel] * g_phase) / g_rate);
      out[frame * 2 + channel] = (int16_t)value;
      int magnitude = value < 0 ? -value : value;
      if (magnitude > g_peak)
        g_peak = magnitude;
    }
    g_phase += SOURCE_RATE;
    while (g_phase >= g_rate) {
      g_phase -= g_rate;
      memcpy(g_previous, g_next, sizeof g_previous);
      source_frame(g_next);
    }
  }
  g_blocks++;
  if ((!g_reported_sound && g_peak) || !(g_blocks % 256)) {
    debugPrintf("[audio] PCM: %lu blocks, streams 0x%05lx, peak %d, invalid %lu\n",
                g_blocks, g_active_mask, g_peak, g_invalid);
    if (!g_reported_sound && !g_peak) {
      const uint8_t *sound = g_sound_interface ? g_sound_interface() : NULL;
      debugPrintf("[audio] silent source: mixer=%p enabled=%d gameDisabled=%d video=%d language=%d array=%p PCM=%p\n",
                  (const void *)sound, sound ? sound[0] : -1,
                  g_game_disabled ? g_game_disabled() : -1, g_video_running ? g_video_running() : -1,
                  g_language ? *g_language : -1, (void *)g_array, (void *)g_buf);
    }
    if (g_peak)
      g_reported_sound = 1;
    g_active_mask = 0;
    g_peak = 0;
  }
}

int jsr_audio_start(void) {
  if (g_pump)
    return 1;
  if (rt_audout_open())
    return 0;
  g_rate = rt_audout_rate();
  if (!g_rate) {
    rt_audout_close();
    return 0;
  }
  g_array = jni_array('S', SOURCE_SAMPLES);
  if (!g_array || !g_array->a.data) {
    jni_release(g_array);
    g_array = NULL;
    rt_audout_close();
    debugPrintf("[audio] no Java short[] for the engine's streams\n");
    return 0;
  }
  g_buf = g_array->a.data;
  g_source_pos = SOURCE_FRAMES;
  g_seeded = 0;
  g_phase = 0;
  g_blocks = g_invalid = g_active_mask = 0;
  g_peak = g_reported_sound = 0;
  g_paused = 0;
  mutexInit(&g_lock);
  rt_audout_cancel(0);
  rt_audout_pause(0);
  g_running = 1;
  g_pump = rt_audout_pump_start(pump, NULL, 0x2A, 2) == 0;
  if (!g_pump) {
    g_running = 0;
    jni_release(g_array);
    g_array = NULL;
    g_buf = NULL;
    rt_audout_close();
  }
  debugPrintf("[audio] %d stereo streams at %u Hz -> audout %u Hz (%s)\n",
              STREAM_COUNT, SOURCE_RATE, (unsigned)g_rate, g_pump ? "pump running" : "PUMP FAILED");
  return g_pump;
}

void jsr_audio_pause(int paused) {
  g_paused = paused;
  rt_audout_pause(paused);
}

void jsr_audio_stop(void) {
  if (!g_pump)
    return;
  g_running = 0;
  rt_audout_pump_stop();
  rt_audout_close();
  jni_release(g_array);
  g_array = NULL;
  g_buf = NULL;
  g_pump = 0;
  debugPrintf("[audio] stopped after %lu blocks (%lu invalid responses)\n", g_blocks, g_invalid);
}
