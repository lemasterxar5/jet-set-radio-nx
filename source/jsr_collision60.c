#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jsr_collision60.h"
#include "util.h"

typedef struct {
  unsigned kind;
  void *collider, *result, *motion, *position;
  uint32_t owner, flags;
  float first[3], second[3], third[3], radius, frame, matrix[16];
} CollisionQuery;

typedef struct CollisionActor {
  struct CollisionActor *next;
  void *player;
  CollisionQuery *queries;
  unsigned count, capacity;
  int valid;
} CollisionActor;

uintptr_t jsr_coli60_ride_tail, jsr_coli60_move_tail, jsr_coli60_object_tail;
uint32_t jsr_coli60_object_literal;
extern void jsr_coli60_original_ride(void *, const float *, const float *, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_move(void *, const float *, const float *, const float *, float, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_object(void *, void *, float, void *, uint32_t, void *);
static CollisionActor *g_actors, *g_capture;
static float **g_matrix;
static int g_installed;

void jsr_collision60_reset(void) {
  while (g_actors) {
    CollisionActor *entry = g_actors;
    g_actors = entry->next;
    free(entry->queries);
    free(entry);
  }
  g_capture = NULL;
}

static CollisionActor *find_actor(void *player, int create) {
  for (CollisionActor *entry = g_actors; entry; entry = entry->next)
    if (entry->player == player)
      return entry;
  if (!create)
    return NULL;
  CollisionActor *entry = calloc(1, sizeof *entry);
  if (entry) {
    entry->player = player;
    entry->next = g_actors;
    g_actors = entry;
  }
  return entry;
}

void jsr_collision60_begin(void *player) {
  g_capture = g_installed ? find_actor(player, 1) : NULL;
  if (g_capture) {
    g_capture->count = 0;
    g_capture->valid = 1;
  }
}

void jsr_collision60_end(void) { g_capture = NULL; }

void jsr_collision60_forget(void *player) {
  CollisionActor *entry = find_actor(player, 0);
  if (entry)
    entry->valid = 0;
}

static void record(CollisionQuery query) {
  if (!g_capture || !g_capture->valid)
    return;
  if (g_capture->count == g_capture->capacity) {
    unsigned capacity = g_capture->capacity ? g_capture->capacity * 2 : 16;
    if (capacity > 4096) {
      g_capture->valid = 0;
      return;
    }
    void *allocation = realloc(g_capture->queries, capacity * sizeof query);
    if (!allocation) {
      g_capture->valid = 0;
      return;
    }
    g_capture->queries = allocation;
    g_capture->capacity = capacity;
  }
  g_capture->queries[g_capture->count++] = query;
}

static void ride(void *collider, const float *first, const float *second, void *result,
                 uint32_t owner, uint32_t flags) {
  if (g_capture) {
    CollisionQuery query = {.kind = 0, .collider = collider, .result = result, .owner = owner, .flags = flags};
    memcpy(query.first, first, sizeof query.first);
    memcpy(query.second, second, sizeof query.second);
    record(query);
  }
  jsr_coli60_original_ride(collider, first, second, result, owner, flags);
}

static void move(void *collider, const float *first, const float *second, const float *third,
                 float radius, void *result, uint32_t owner, uint32_t flags) {
  if (g_capture) {
    CollisionQuery query = {.kind = 1, .collider = collider, .result = result, .owner = owner, .flags = flags, .radius = radius};
    memcpy(query.first, first, sizeof query.first);
    memcpy(query.second, second, sizeof query.second);
    memcpy(query.third, third, sizeof query.third);
    record(query);
  }
  jsr_coli60_original_move(collider, first, second, third, radius, result, owner, flags);
}

static void object(void *collider, void *motion, float frame, void *result, uint32_t owner, void *position) {
  if (g_capture) {
    if (!g_matrix || !*g_matrix) {
      g_capture->valid = 0;
    } else {
      CollisionQuery query = {.kind = 2, .collider = collider, .result = result, .motion = motion,
                              .position = position, .owner = owner, .frame = frame};
      memcpy(query.matrix, *g_matrix, sizeof query.matrix);
      record(query);
    }
  }
  jsr_coli60_original_object(collider, motion, frame, result, owner, position);
}

int jsr_collision60_hold(void *player) {
  CollisionActor *entry = find_actor(player, 0);
  if (!entry || !entry->valid || !g_matrix || !*g_matrix)
    return 0;
  float saved[16];
  float *matrix = *g_matrix;
  memcpy(saved, matrix, sizeof saved);
  for (unsigned index = 0; index < entry->count; index++) {
    const CollisionQuery *query = &entry->queries[index];
    if (query->kind == 0)
      jsr_coli60_original_ride(query->collider, query->first, query->second, query->result, query->owner, query->flags);
    else if (query->kind == 1)
      jsr_coli60_original_move(query->collider, query->first, query->second, query->third, query->radius,
                             query->result, query->owner, query->flags);
    else {
      memcpy(matrix, query->matrix, sizeof saved);
      jsr_coli60_original_object(query->collider, query->motion, query->frame, query->result, query->owner, query->position);
      memcpy(matrix, saved, sizeof saved);
    }
  }
  return 1;
}

int jsr_collision60_install(so_module *module) {
  static const struct {
    uintptr_t offset;
    uint8_t prefix[8];
    void *replacement;
    uintptr_t *tail;
  } hooks[] = {
    {0x1b6280, {0xf8,0xb5,0x1d,0x46,0x2d,0xed,0x02,0x8b}, ride, &jsr_coli60_ride_tail},
    {0x1b6478, {0x2d,0xe9,0xf0,0x41,0x2d,0xed,0x02,0x8b}, move, &jsr_coli60_move_tail},
    {0x1b6de4, {0x2d,0xe9,0xf0,0x41,0x2a,0x4d,0x84,0xb0}, object, &jsr_coli60_object_tail},
  };
  g_installed = 0;
  if (module->state != SO_STAGED || module->load_size < 0x63d83c)
    return 0;
  uintptr_t base = (uintptr_t)module->load_virtbase;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++)
    if (memcmp((uint8_t *)module->load_base + hooks[index].offset, hooks[index].prefix, 8))
      return 0;
  memcpy(&jsr_coli60_object_literal, (uint8_t *)module->load_base + 0x1b6e94, 4);
  g_matrix = (float **)(base + 0x63d838);
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uint32_t stub[] = {0xf000f8df, (uint32_t)(uintptr_t)hooks[index].replacement};
    memcpy((uint8_t *)module->load_base + hooks[index].offset, stub, sizeof stub);
    *hooks[index].tail = (base + hooks[index].offset + 8) | 1u;
  }
  jsr_collision60_reset();
  g_installed = 1;
  debugPrintf("[native60] recorded collision queries renewed every engine frame; no retained result-list pointers\n");
  return 1;
}
