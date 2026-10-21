#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jsr_pose60.h"
#include "util.h"

#define POSE_LIMIT 4096u
#define POSE_MEMORY_LIMIT (16u * 1024u * 1024u)

typedef struct {
  void *object, *destination;
  unsigned destination_frame;
  const void *first, *second;
  float first_frame, second_frame, blend, blend_step;
  int native_blend;
} PoseCommand;

typedef struct {
  const void *motion;
  float frame, step, start, end;
  int flags, extended;
} PoseMotion;

typedef struct {
  void *header, *data;
  size_t size, capacity;
  uint8_t initial_header[8], restore_header[8];
  uint8_t *initial_data, *restore_data;
} PoseBuffer;

typedef struct PosePlayer {
  struct PosePlayer *next;
  void *player;
  unsigned commands, motions, buffers, previous_commands;
  unsigned command_capacity, motion_capacity, buffer_capacity, previous_capacity;
  int valid;
  float phase;
  PoseCommand *command, *previous;
  PoseMotion *motion;
  PoseBuffer *buffer;
} PosePlayer;

uintptr_t jsr_pose_copy_tail, jsr_pose_compose_tail, jsr_pose_draw_tail;
extern int jsr_pose_original_copy(void *, void *, unsigned, const void *, unsigned);
extern int jsr_pose_original_compose(void *, void *, unsigned, const void *, const void *, float, float, float);
extern void jsr_pose_original_draw(void *);
extern int jsr_n60_original_motion(const void *, float *, float, int);
extern int jsr_n60_original_motion_ex(const void *, float *, float, float, float, int);

static PosePlayer *g_players, *g_capture;
static const uint32_t *g_formats;
static void (*g_count_models)(const void *, unsigned *);
static const void *(*g_motion_type)(void *, int);
static uintptr_t g_game_base;
static unsigned long g_resampled;
static int g_installed, g_drawing, g_evaluation_depth;

void jsr_pose60_reset(void) {
  while (g_players) {
    PosePlayer *entry = g_players;
    g_players = entry->next;
    for (unsigned index = 0; index < entry->buffer_capacity; index++) {
      free(entry->buffer[index].initial_data);
      free(entry->buffer[index].restore_data);
    }
    free(entry->buffer);
    free(entry->command);
    free(entry->previous);
    free(entry->motion);
    free(entry);
  }
  g_capture = NULL;
  g_resampled = 0;
  g_drawing = g_evaluation_depth = 0;
}

static PosePlayer *find_player(void *player, int create) {
  for (PosePlayer *entry = g_players; entry; entry = entry->next)
    if (entry->player == player)
      return entry;
  if (!create)
    return NULL;
  PosePlayer *entry = calloc(1, sizeof *entry);
  if (entry) {
    entry->player = player;
    entry->next = g_players;
    g_players = entry;
  }
  return entry;
}

static int reserve(void **array, unsigned *capacity, unsigned count, size_t stride) {
  if (count <= *capacity)
    return 1;
  if (count > POSE_LIMIT)
    return 0;
  unsigned expanded = *capacity ? *capacity * 2 : 8;
  while (expanded < count)
    expanded *= 2;
  if (expanded > POSE_LIMIT)
    expanded = POSE_LIMIT;
  void *allocation = realloc(*array, expanded * stride);
  if (!allocation)
    return 0;
  memset((uint8_t *)allocation + *capacity * stride, 0, (expanded - *capacity) * stride);
  *array = allocation;
  *capacity = expanded;
  return 1;
}

void jsr_pose60_begin(void *player) {
  if (!g_installed)
    return;
  g_capture = find_player(player, 1);
  if (!g_capture)
    return;
  PoseCommand *reusable = g_capture->previous;
  unsigned capacity = g_capture->previous_capacity;
  g_capture->previous = g_capture->command;
  g_capture->previous_capacity = g_capture->command_capacity;
  g_capture->previous_commands = g_capture->valid ? g_capture->commands : 0;
  g_capture->command = reusable;
  g_capture->command_capacity = capacity;
  g_capture->commands = g_capture->motions = g_capture->buffers = 0;
  g_capture->valid = 1;
}

void jsr_pose60_end(void) { g_capture = NULL; }

void jsr_pose60_forget(void *player) {
  PosePlayer *entry = find_player(player, 0);
  if (entry)
    entry->valid = 0;
}

void jsr_pose60_phase(void *player, float phase) {
  PosePlayer *entry = find_player(player, 0);
  if (entry)
    entry->phase = isfinite(phase) && phase > 0.0f && phase < 1.0f ? phase : 0.0f;
}

void jsr_pose60_motion(const void *motion, float frame, float step, int flags, int extended, float start, float end) {
  if (!g_capture)
    return;
  if (!isfinite(frame) || !isfinite(step) || (extended && (!isfinite(start) || !isfinite(end))) ||
      !reserve((void **)&g_capture->motion, &g_capture->motion_capacity,
               g_capture->motions + 1, sizeof *g_capture->motion)) {
    g_capture->valid = 0;
    return;
  }
  g_capture->motion[g_capture->motions++] = (PoseMotion){motion, frame, step, start, end, flags, extended};
}

static int buffer_layout(void *object, void *header, void **data, size_t *size, unsigned *frames) {
  if (!object || !header)
    return 0;
  uint16_t description[2];
  memcpy(description, header, sizeof description);
  if (description[0] >= 128 || !description[1])
    return 0;
  unsigned format = g_formats[description[0]], models = 0;
  if (format > 2)
    return 0;
  g_count_models(object, &models);
  if (!models)
    return 0;
  uint64_t samples = description[1];
  uint64_t stride = 16 + (format == 0 ? samples * 36 : format == 1 ? samples * 40 : samples * 30 + (samples & 1) * 2);
  uint64_t bytes = models * stride;
  if (!bytes || bytes > POSE_MEMORY_LIMIT)
    return 0;
  memcpy(data, (uint8_t *)header + 4, 4);
  *size = (size_t)bytes;
  *frames = description[1];
  return *data != NULL;
}

static int snapshot(PosePlayer *entry, const PoseCommand *command) {
  void *data;
  size_t size;
  unsigned frames;
  if (!buffer_layout(command->object, command->destination, &data, &size, &frames) ||
      command->destination_frame >= frames)
    return 0;
  for (unsigned index = 0; index < entry->buffers; index++) {
    PoseBuffer *buffer = &entry->buffer[index];
    if (buffer->header == command->destination)
      return buffer->data == data && buffer->size == size && !memcmp(buffer->initial_header, command->destination, 8);
  }
  if (!reserve((void **)&entry->buffer, &entry->buffer_capacity, entry->buffers + 1, sizeof *entry->buffer))
    return 0;
  PoseBuffer *buffer = &entry->buffer[entry->buffers];
  if (buffer->capacity < size) {
    uint8_t *initial = malloc(size), *restore = malloc(size);
    if (!initial || !restore) {
      free(initial);
      free(restore);
      return 0;
    }
    free(buffer->initial_data);
    free(buffer->restore_data);
    buffer->initial_data = initial;
    buffer->restore_data = restore;
    buffer->capacity = size;
  }
  buffer->header = command->destination;
  buffer->data = data;
  buffer->size = size;
  memcpy(buffer->initial_header, buffer->header, 8);
  memcpy(buffer->initial_data, data, size);
  for (unsigned index = 0; index < entry->buffers; index++) {
    const PoseBuffer *earlier = &entry->buffer[index];
    uintptr_t first = (uintptr_t)data, last = first + size;
    uintptr_t overlap = (uintptr_t)earlier->data, overlap_end = overlap + earlier->size;
    if (overlap < first)
      overlap = first;
    if (overlap_end > last)
      overlap_end = last;
    if (overlap < overlap_end)
      memcpy(buffer->initial_data + overlap - first,
             earlier->initial_data + overlap - (uintptr_t)earlier->data, overlap_end - overlap);
  }
  entry->buffers++;
  return 1;
}

static void record(PoseCommand command) {
  if (!g_capture || g_evaluation_depth)
    return;
  if (!isfinite(command.first_frame) || !isfinite(command.second_frame) || !isfinite(command.blend) ||
      !reserve((void **)&g_capture->command, &g_capture->command_capacity,
               g_capture->commands + 1, sizeof *g_capture->command) || !snapshot(g_capture, &command)) {
    g_capture->valid = 0;
    return;
  }
  unsigned index = g_capture->commands++;
  if (!command.native_blend && index < g_capture->previous_commands && command.blend >= 0.0f && command.blend <= 1.0f) {
    const PoseCommand *previous = &g_capture->previous[index];
    if (previous->object == command.object && previous->destination == command.destination &&
        previous->destination_frame == command.destination_frame && previous->first == command.first &&
        previous->second == command.second && previous->blend >= 0.0f && previous->blend <= 1.0f)
      command.blend_step = command.blend - previous->blend;
  }
  g_capture->command[index] = command;
}

static int copy_motion(void *object, void *destination, unsigned destination_frame,
                       const void *source, unsigned source_frame) {
  record((PoseCommand){object, destination, destination_frame, source, source,
                       (float)source_frame, (float)source_frame, 0.0f, 0.0f, 1});
  g_evaluation_depth++;
  int result = jsr_pose_original_copy(object, destination, destination_frame, source, source_frame);
  g_evaluation_depth--;
  if (!result && g_capture)
    g_capture->valid = 0;
  return result;
}

static int native_blend_rate(uintptr_t caller, float *rate) {
  caller &= ~(uintptr_t)1;
  unsigned offset;
  if (caller == g_game_base + 0x1e8556)
    offset = 24;
  else if (caller == g_game_base + 0x1e85d6)
    offset = 16;
  else
    return 0;
  if (!g_capture || !g_motion_type)
    return 0;
  int motion;
  memcpy(&motion, (uint8_t *)g_capture->player + 0x78, sizeof motion);
  const void *type = g_motion_type(g_capture->player, motion);
  if (!type)
    return 0;
  memcpy(rate, (const uint8_t *)type + offset, sizeof *rate);
  return isfinite(*rate);
}

static int compose(void *object, void *destination, unsigned destination_frame,
                   const void *first, const void *second, float first_frame, float second_frame, float blend) {
  PoseCommand command = {object, destination, destination_frame, first, second, first_frame, second_frame, blend, 0.0f, 0};
  command.native_blend = native_blend_rate((uintptr_t)__builtin_return_address(0), &command.blend_step);
  record(command);
  g_evaluation_depth++;
  int result = jsr_pose_original_compose(object, destination, destination_frame, first, second, first_frame, second_frame, blend);
  g_evaluation_depth--;
  if (!result && g_capture)
    g_capture->valid = 0;
  return result;
}

static float advance(const PosePlayer *entry, const void *motion, float frame) {
  const PoseMotion *selected = NULL;
  float distance = INFINITY;
  for (unsigned index = 0; index < entry->motions; index++) {
    const PoseMotion *channel = &entry->motion[index];
    float current = fabsf(frame - channel->frame);
    if (channel->motion == motion && current < distance) {
      selected = channel;
      distance = current;
    }
  }
  if (selected) {
    float step = selected->step * entry->phase;
    if (selected->extended)
      jsr_n60_original_motion_ex(motion, &frame, step, selected->start, selected->end, selected->flags);
    else
      jsr_n60_original_motion(motion, &frame, step, selected->flags);
  }
  return frame;
}

static void restore_buffers(PosePlayer *entry) {
  for (unsigned index = 0; index < entry->buffers; index++) {
    PoseBuffer *buffer = &entry->buffer[index];
    memcpy(buffer->header, buffer->restore_header, 8);
    memcpy(buffer->data, buffer->restore_data, buffer->size);
  }
}

static void draw(void *player) {
  PosePlayer *entry = find_player(player, 0);
  if (g_drawing || !entry || !entry->valid || !entry->commands || entry->phase == 0.0f) {
    jsr_pose_original_draw(player);
    return;
  }
  for (unsigned index = 0; index < entry->buffers; index++) {
    PoseBuffer *buffer = &entry->buffer[index];
    if (memcmp(buffer->header, buffer->initial_header, 8)) {
      entry->valid = 0;
      jsr_pose_original_draw(player);
      return;
    }
    memcpy(buffer->restore_header, buffer->header, 8);
    memcpy(buffer->restore_data, buffer->data, buffer->size);
  }
  g_drawing = 1;
  for (unsigned index = 0; index < entry->buffers; index++) {
    PoseBuffer *buffer = &entry->buffer[index];
    memcpy(buffer->data, buffer->initial_data, buffer->size);
  }
  int evaluated = 1;
  for (unsigned index = 0; index < entry->commands; index++) {
    const PoseCommand *command = &entry->command[index];
    float blend = command->blend + command->blend_step * entry->phase;
    if (command->blend >= 0.0f && command->blend <= 1.0f)
      blend = fminf(1.0f, fmaxf(0.0f, blend));
    if (!jsr_pose_original_compose(command->object, command->destination, command->destination_frame,
                                   command->first, command->second,
                                   advance(entry, command->first, command->first_frame),
                                   advance(entry, command->second, command->second_frame), blend)) {
      evaluated = entry->valid = 0;
      break;
    }
  }
  if (!evaluated)
    restore_buffers(entry);
  jsr_pose_original_draw(player);
  if (evaluated) {
    restore_buffers(entry);
    if (!(g_resampled++ % 600))
      debugPrintf("[pose60] evaluated=%lu player=%p commands=%u channels=%u phase=%.3f\n",
                  g_resampled, player, entry->commands, entry->motions, (double)entry->phase);
  }
  g_drawing = 0;
}

int jsr_pose60_install(so_module *module) {
  g_installed = 0;
  static const struct {
    uintptr_t offset;
    uint8_t prefix[8];
    void *replacement;
    uintptr_t *tail;
  } hooks[] = {
    {0x1ae0d8, {0x70,0xb5,0x0c,0x88,0x84,0xb0,0x7f,0x2c}, copy_motion, &jsr_pose_copy_tail},
    {0x1ae128, {0x2d,0xe9,0xf0,0x4f,0x2d,0xed,0x02,0x8b}, compose, &jsr_pose_compose_tail},
    {0x1d6178, {0x2d,0xe9,0xf0,0x47,0x2d,0xed,0x04,0x8b}, draw, &jsr_pose_draw_tail},
  };
  if (module->state != SO_STAGED || module->load_size < 0x290f78 + 128 * 4)
    return 0;
  static const uint8_t count_prefix[] = {0x2d,0xe9,0xf0,0x4f,0x83,0xb0,0x01,0x90};
  static const uint8_t type_prefix[] = {0xd0,0xf8,0xdc,0x31,0x2b,0xb9,0x06,0x4b};
  if (memcmp((uint8_t *)module->load_base + 0x1a6614, count_prefix, sizeof count_prefix) ||
      memcmp((uint8_t *)module->load_base + 0x1e99c0, type_prefix, sizeof type_prefix))
    return 0;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++)
    if (memcmp((uint8_t *)module->load_base + hooks[index].offset, hooks[index].prefix, 8))
      return 0;
  uintptr_t base = (uintptr_t)module->load_virtbase;
  g_count_models = (void *)(base + 0x1a6614 + 1);
  g_motion_type = (void *)(base + 0x1e99c0 + 1);
  g_formats = (const uint32_t *)(base + 0x290f78);
  g_game_base = base;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uint32_t stub[] = {0xf000f8df, (uint32_t)(uintptr_t)hooks[index].replacement};
    memcpy((uint8_t *)module->load_base + hooks[index].offset, stub, sizeof stub);
    *hooks[index].tail = (base + hooks[index].offset + 8) | 1u;
  }
  jsr_pose60_reset();
  g_installed = 1;
  debugPrintf("[pose60] native clip/transition evaluation; dynamic snapshots preserve feedback and multiframe buffers\n");
  return 1;
}
