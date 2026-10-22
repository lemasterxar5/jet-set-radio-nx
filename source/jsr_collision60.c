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
  uint32_t identity[4];
  uint64_t last_used;
} CollisionActor;

uintptr_t jsr_coli60_ride_tail, jsr_coli60_move_tail, jsr_coli60_object_tail;
uintptr_t jsr_coli60_graffiti_pos_tail, jsr_coli60_graffiti_ignore_tail, jsr_coli60_graffiti_range_tail;
uintptr_t jsr_coli60_graffiti_enemy_range_tail;
uintptr_t jsr_coli60_follow_tail;
uint32_t jsr_coli60_object_literal;
extern void jsr_coli60_original_ride(void *, const float *, const float *, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_move(void *, const float *, const float *, const float *, float, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_object(void *, void *, float, void *, uint32_t, void *);
extern void jsr_coli60_original_graffiti_pos(const float *, void *);
extern void jsr_coli60_original_graffiti_ignore(const float *, void *);
extern void jsr_coli60_original_graffiti_range(void *, void *);
extern void jsr_coli60_original_graffiti_enemy_range(const float *, void *, void *);
extern void jsr_coli60_original_follow(void *, void *);
static CollisionActor *g_actors, *g_capture;
static float **g_matrix;
static int g_installed;
static uint64_t g_generation;
static unsigned g_actor_count;
static size_t g_query_bytes;
static const uint32_t *g_result_pool;
static uintptr_t g_camera_callback;
static unsigned long g_expired_results;

static void actor_identity(void *player, uint32_t identity[4]) {
  static const unsigned player_offsets[] = {0x04, 0x404, 0x418, 0x4e0};
  static const unsigned camera_offsets[] = {0x04, 0x238, 0x240, 0x248};
  uint32_t callback;
  memcpy(&callback, (uint8_t *)player + 0x08, sizeof callback);
  const unsigned *offsets = g_camera_callback && callback == g_camera_callback ? camera_offsets : player_offsets;
  for (unsigned index = 0; index < 4; index++)
    memcpy(&identity[index], (uint8_t *)player + offsets[index], sizeof identity[index]);
}

static int current_result(uint32_t node) {
  unsigned last = g_result_pool[1];
  if (last >= 50)
    return 0;
  for (unsigned index = 0; index <= last; index++) {
    uint32_t block = g_result_pool[2 + index];
    unsigned used = index < last ? 50 : g_result_pool[0xd0 / 4];
    if (used > 50)
      return 0;
    if (block && node >= block && node - block < used * 20 && !((node - block) % 20))
      return 1;
  }
  return 0;
}

void jsr_collision60_prepare(void *player) {
  static const unsigned player_heads[] = {0x408, 0x41c, 0x424, 0x544};
  static const unsigned camera_heads[] = {0x23c, 0x244, 0x24c};
  if (!g_installed || !g_result_pool || !player)
    return;
  uint32_t callback;
  memcpy(&callback, (uint8_t *)player + 0x08, sizeof callback);
  int camera = g_camera_callback && callback == g_camera_callback;
  const unsigned *heads = camera ? camera_heads : player_heads;
  unsigned count_heads = camera ? 3 : 4;
  for (unsigned index = 0; index < count_heads; index++) {
    uint32_t head;
    memcpy(&head, (uint8_t *)player + heads[index], sizeof head);
    uint32_t node = head;
    unsigned count = 0;
    while (node && count++ < 2500 && current_result(node))
      memcpy(&node, (const uint8_t *)(uintptr_t)node + 16, sizeof node);
    if (node) {
      uint32_t empty = 0;
      memcpy((uint8_t *)player + heads[index], &empty, sizeof empty);
      if (g_expired_results++ < 16)
        debugPrintf("[native60] expired ride results discarded: player=%p slot=0x%x head=0x%lx invalid=0x%lx\n",
                    player, heads[index], (unsigned long)head, (unsigned long)node);
    }
  }
}

static void release_actor(CollisionActor *entry) {
  g_query_bytes -= entry->capacity * sizeof *entry->queries;
  free(entry->queries);
  free(entry);
  g_actor_count--;
}

void jsr_collision60_tick(void) {
  g_generation++;
  for (CollisionActor **link = &g_actors; *link;) {
    CollisionActor *entry = *link;
    if (entry != g_capture && entry->last_used + 2 < g_generation) {
      *link = entry->next;
      release_actor(entry);
    } else {
      link = &entry->next;
    }
  }
}

void jsr_collision60_reset(void) {
  while (g_actors) {
    CollisionActor *entry = g_actors;
    g_actors = entry->next;
    release_actor(entry);
  }
  g_capture = NULL;
  g_generation = 0;
}

static CollisionActor *find_actor(void *player, int create) {
  for (CollisionActor *entry = g_actors; entry; entry = entry->next)
    if (entry->player == player) {
      entry->last_used = g_generation;
      return entry;
    }
  if (!create || g_actor_count >= 128)
    return NULL;
  CollisionActor *entry = calloc(1, sizeof *entry);
  if (entry) {
    entry->player = player;
    entry->last_used = g_generation;
    g_actor_count++;
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
    actor_identity(player, g_capture->identity);
  }
}

void jsr_collision60_end(void) { g_capture = NULL; }

void jsr_collision60_forget(void *player) {
  for (CollisionActor **link = &g_actors; *link; link = &(*link)->next) {
    CollisionActor *entry = *link;
    if (entry->player == player) {
      if (entry == g_capture) {
        entry->valid = 0;
      } else {
        *link = entry->next;
        release_actor(entry);
      }
      return;
    }
  }
}

static void record(CollisionQuery query) {
  if (!g_capture || !g_capture->valid)
    return;
  if (g_capture->count == g_capture->capacity) {
    unsigned capacity = g_capture->capacity ? g_capture->capacity * 2 : 16;
    size_t extra = (capacity - g_capture->capacity) * sizeof query;
    if (capacity > 4096 || extra > 8u * 1024u * 1024u - g_query_bytes) {
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
    g_query_bytes += extra;
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

static void graffiti_pos(const float *position, void *result) {
  if (g_capture && position) {
    CollisionQuery query = {.kind = 3, .result = result};
    memcpy(query.first, position, sizeof query.first);
    record(query);
  }
  jsr_coli60_original_graffiti_pos(position, result);
}

static void graffiti_ignore(const float *position, void *result) {
  if (g_capture && position) {
    CollisionQuery query = {.kind = 4, .result = result};
    memcpy(query.first, position, sizeof query.first);
    record(query);
  }
  jsr_coli60_original_graffiti_ignore(position, result);
}

static void graffiti_range(void *range, void *result) {
  if (g_capture && range)
    record((CollisionQuery){.kind = 5, .collider = range, .result = result});
  jsr_coli60_original_graffiti_range(range, result);
}

static void graffiti_enemy_range(const float *position, void *range, void *result) {
  if (g_capture && position && range) {
    CollisionQuery query = {.kind = 6, .collider = range, .result = result};
    memcpy(query.first, position, sizeof query.first);
    record(query);
  }
  jsr_coli60_original_graffiti_enemy_range(position, range, result);
}

static void follow(void *position, void *result) {
  if (g_capture && position)
    record((CollisionQuery){.kind = 7, .position = position, .result = result});
  jsr_coli60_original_follow(position, result);
}

int jsr_collision60_hold(void *player) {
  CollisionActor *entry = find_actor(player, 0);
  if (!entry || !entry->valid || !g_matrix || !*g_matrix)
    return 0;
  uint32_t identity[4];
  actor_identity(player, identity);
  if (memcmp(identity, entry->identity, sizeof identity)) {
    entry->valid = 0;
    return 0;
  }
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
    else if (query->kind == 2) {
      memcpy(matrix, query->matrix, sizeof saved);
      jsr_coli60_original_object(query->collider, query->motion, query->frame, query->result, query->owner, query->position);
      memcpy(matrix, saved, sizeof saved);
    } else if (query->kind == 3)
      jsr_coli60_original_graffiti_pos(query->first, query->result);
    else if (query->kind == 4)
      jsr_coli60_original_graffiti_ignore(query->first, query->result);
    else if (query->kind == 5)
      jsr_coli60_original_graffiti_range(query->collider, query->result);
    else if (query->kind == 6)
      jsr_coli60_original_graffiti_enemy_range(query->first, query->collider, query->result);
    else if (query->kind == 7)
      jsr_coli60_original_follow(query->position, query->result);
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
    {0x1bb52c, {0xf0,0xb4,0x28,0xb3,0x09,0xb1,0x00,0x23}, graffiti_pos, &jsr_coli60_graffiti_pos_tail},
    {0x1bb590, {0xf0,0xb4,0x28,0xb3,0x09,0xb1,0x00,0x23}, graffiti_ignore, &jsr_coli60_graffiti_ignore_tail},
    {0x1bb630, {0x70,0xb4,0x90,0xb1,0x09,0xb1,0x00,0x23}, graffiti_range, &jsr_coli60_graffiti_range_tail},
    {0x1bb66c, {0xf0,0xb4,0x12,0xb1,0x4f,0xf0,0xff,0x33}, graffiti_enemy_range, &jsr_coli60_graffiti_enemy_range_tail},
    {0x1b8d18, {0x70,0xb4,0x09,0xb1,0x00,0x23,0x0b,0x60}, follow, &jsr_coli60_follow_tail},
  };
  g_installed = 0;
  if (module->state != SO_STAGED || module->load_size < 0x63d83c)
    return 0;
  static const uint8_t result_prefix[] = {0xf0,0xb5,0x2d,0xed,0x02,0x8b,0x56,0x4c};
  if (memcmp((uint8_t *)module->load_base + 0x1b2304, result_prefix, sizeof result_prefix))
    return 0;
  uintptr_t base = (uintptr_t)module->load_virtbase;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++)
    if (memcmp((uint8_t *)module->load_base + hooks[index].offset, hooks[index].prefix, 8))
      return 0;
  memcpy(&jsr_coli60_object_literal, (uint8_t *)module->load_base + 0x1b6e94, 4);
  g_matrix = (float **)(base + 0x63d838);
  g_result_pool = (const uint32_t *)(base + 0x3d8bd8);
  g_camera_callback = base + 0x1d90b5;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uint32_t stub[] = {0xf000f8df, (uint32_t)(uintptr_t)hooks[index].replacement};
    memcpy((uint8_t *)module->load_base + hooks[index].offset, stub, sizeof stub);
    *hooks[index].tail = (base + hooks[index].offset + 8) | 1u;
  }
  jsr_collision60_reset();
  g_installed = 1;
  debugPrintf("[native60] collision and graffiti queries renewed every engine frame; live-to-replay cache retained\n");
  return 1;
}
