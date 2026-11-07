#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

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
  int npc;
  uint32_t identity[4];
  uint64_t last_used;
} CollisionActor;

uintptr_t jsr_coli60_ride_tail, jsr_coli60_move_tail, jsr_coli60_object_tail;
uintptr_t jsr_coli60_graffiti_pos_tail, jsr_coli60_graffiti_ignore_tail, jsr_coli60_graffiti_range_tail;
uintptr_t jsr_coli60_graffiti_enemy_range_tail;
uintptr_t jsr_coli60_follow_tail;
uintptr_t jsr_coli60_static_object_tail, jsr_coli60_simple_move_tail, jsr_coli60_ground_tail;
uintptr_t jsr_coli60_enemy_pos_tail;
uint32_t jsr_coli60_enemy_pos_literal;
uint32_t jsr_coli60_object_literal;
extern void jsr_coli60_original_ride(void *, const float *, const float *, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_move(void *, const float *, const float *, const float *, float, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_object(void *, void *, float, void *, uint32_t, void *);
extern void jsr_coli60_original_graffiti_pos(const float *, void *);
extern void jsr_coli60_original_graffiti_ignore(const float *, void *);
extern void jsr_coli60_original_graffiti_range(void *, void *);
extern void jsr_coli60_original_graffiti_enemy_range(const float *, void *, void *);
extern void jsr_coli60_original_follow(void *, void *);
extern void jsr_coli60_original_static_object(void *, void *, uint32_t);
extern void jsr_coli60_original_simple_move(void *, const float *, void *, uint32_t, uint32_t);
extern void jsr_coli60_original_ground(void *, void *, uint32_t);
extern void jsr_coli60_original_enemy_pos(const float *);
static CollisionActor *g_actors, *g_capture;
static float **g_matrix;
static int g_installed;
static uint64_t g_generation;
static unsigned g_actor_count;
static size_t g_query_bytes;
static const uint32_t *g_result_pool;
static uintptr_t g_camera_callback;
static unsigned long g_expired_results;
static Mutex g_cache_mutex;
static void (*g_original_free)(void *);

static void game_free(void *pointer) {
  if (!pointer)
    return;
  mutexLock(&g_cache_mutex);
  for (CollisionActor *entry = g_actors; entry; entry = entry->next) {
    if (!entry->npc || !entry->valid)
      continue;
    if (entry->player == pointer || entry->identity[2] == (uint32_t)(uintptr_t)pointer ||
        entry->identity[3] == (uint32_t)(uintptr_t)pointer)
      entry->valid = 0;
    for (unsigned index = 0; index < entry->count && entry->valid; index++) {
      const CollisionQuery *query = &entry->queries[index];
      if (query->collider == pointer || query->result == pointer ||
          query->motion == pointer || query->position == pointer)
        entry->valid = 0;
    }
  }
  g_original_free(pointer);
  mutexUnlock(&g_cache_mutex);
}

static void npc_identity(void *object, uint32_t identity[4]) {
  static const unsigned offsets[] = {0x04, 0x08, 0x18, 0x28};
  for (unsigned index = 0; index < 4; index++)
    memcpy(&identity[index], (uint8_t *)object + offsets[index], sizeof identity[index]);
}

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
  mutexLock(&g_cache_mutex);
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
  mutexUnlock(&g_cache_mutex);
}

void jsr_collision60_reset(void) {
  mutexLock(&g_cache_mutex);
  while (g_actors) {
    CollisionActor *entry = g_actors;
    g_actors = entry->next;
    release_actor(entry);
  }
  g_capture = NULL;
  g_generation = 0;
  mutexUnlock(&g_cache_mutex);
}

static CollisionActor *find_actor(void *player, int create) {
  for (CollisionActor *entry = g_actors; entry; entry = entry->next)
    if (entry->player == player) {
      entry->last_used = g_generation;
      return entry;
    }
  if (!create || g_actor_count >= 512)
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
  mutexLock(&g_cache_mutex);
  g_capture = g_installed ? find_actor(player, 1) : NULL;
  if (g_capture) {
    g_capture->count = 0;
    g_capture->valid = 1;
    g_capture->npc = 0;
    actor_identity(player, g_capture->identity);
  }
  mutexUnlock(&g_cache_mutex);
}

void jsr_collision60_begin_npc(void *object) {
  mutexLock(&g_cache_mutex);
  g_capture = g_installed ? find_actor(object, 1) : NULL;
  if (g_capture) {
    g_capture->count = 0;
    g_capture->valid = 1;
    g_capture->npc = 1;
    npc_identity(object, g_capture->identity);
  }
  mutexUnlock(&g_cache_mutex);
}

void jsr_collision60_prepare_npc(void *object) {
  mutexLock(&g_cache_mutex);
  CollisionActor *entry = find_actor(object, 0);
  if (entry && entry->npc) {
    uint32_t identity[4];
    npc_identity(object, identity);
    if (memcmp(identity, entry->identity, sizeof identity))
      entry->valid = 0;
  }
  if (entry && entry->valid && entry->npc && g_result_pool) {
    for (unsigned index = 0; index < entry->count; index++) {
      const CollisionQuery *query = &entry->queries[index];
      if (!query->result || !(query->kind == 0 || query->kind == 1 || query->kind == 10 || query->kind == 11))
        continue;
      uint32_t node;
      memcpy(&node, query->result, sizeof node);
      unsigned length = 0;
      while (node && length++ < 2500 && current_result(node))
        memcpy(&node, (uint8_t *)(uintptr_t)node + 16, sizeof node);
      if (node)
        memset(query->result, 0, sizeof node);
    }
  }
  mutexUnlock(&g_cache_mutex);
}

void jsr_collision60_end(void) {
  mutexLock(&g_cache_mutex);
  g_capture = NULL;
  mutexUnlock(&g_cache_mutex);
}

void jsr_collision60_forget(void *player) {
  mutexLock(&g_cache_mutex);
  for (CollisionActor **link = &g_actors; *link; link = &(*link)->next) {
    CollisionActor *entry = *link;
    if (entry->player == player) {
      if (entry == g_capture) {
        entry->valid = 0;
      } else {
        *link = entry->next;
        release_actor(entry);
      }
      break;
    }
  }
  mutexUnlock(&g_cache_mutex);
}

static void record(CollisionQuery query) {
  mutexLock(&g_cache_mutex);
  if (!g_capture || !g_capture->valid)
    goto done;
  if (g_capture->count == g_capture->capacity) {
    unsigned capacity = g_capture->capacity ? g_capture->capacity * 2 : 16;
    size_t extra = (capacity - g_capture->capacity) * sizeof query;
    if (capacity > 4096 || extra > 8u * 1024u * 1024u - g_query_bytes) {
      g_capture->valid = 0;
      goto done;
    }
    void *allocation = realloc(g_capture->queries, capacity * sizeof query);
    if (!allocation) {
      g_capture->valid = 0;
      goto done;
    }
    g_capture->queries = allocation;
    g_capture->capacity = capacity;
    g_query_bytes += extra;
  }
  g_capture->queries[g_capture->count++] = query;
done:
  mutexUnlock(&g_cache_mutex);
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

static void static_object(void *collider, void *result, uint32_t owner) {
  record((CollisionQuery){.kind = 8, .collider = collider, .result = result, .owner = owner});
  jsr_coli60_original_static_object(collider, result, owner);
}

static void enemy_pos(const float *position) {
  if (position) {
    CollisionQuery query = {.kind = 9};
    memcpy(query.first, position, sizeof query.first);
    record(query);
  }
  jsr_coli60_original_enemy_pos(position);
}

static void simple_move(void *collider, const float *sphere, void *result, uint32_t owner, uint32_t flags) {
  CollisionQuery query = {.kind = 10, .collider = collider, .result = result, .owner = owner, .flags = flags};
  memcpy(query.matrix, sphere, sizeof(float) * 4);
  record(query);
  jsr_coli60_original_simple_move(collider, sphere, result, owner, flags);
}

static void ground(void *collider, void *result, uint32_t owner) {
  record((CollisionQuery){.kind = 11, .collider = collider, .result = result, .owner = owner});
  jsr_coli60_original_ground(collider, result, owner);
}

int jsr_collision60_hold(void *player) {
  mutexLock(&g_cache_mutex);
  CollisionActor *entry = find_actor(player, 0);
  if (!entry || !entry->valid || !g_matrix || !*g_matrix) {
    mutexUnlock(&g_cache_mutex);
    return 0;
  }
  uint32_t identity[4];
  if (entry->npc)
    npc_identity(player, identity);
  else
    actor_identity(player, identity);
  if (memcmp(identity, entry->identity, sizeof identity)) {
    entry->valid = 0;
    mutexUnlock(&g_cache_mutex);
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
    else if (query->kind == 8)
      jsr_coli60_original_static_object(query->collider, query->result, query->owner);
    else if (query->kind == 9)
      jsr_coli60_original_enemy_pos(query->first);
    else if (query->kind == 10)
      jsr_coli60_original_simple_move(query->collider, query->matrix, query->result, query->owner, query->flags);
    else if (query->kind == 11)
      jsr_coli60_original_ground(query->collider, query->result, query->owner);
  }
  mutexUnlock(&g_cache_mutex);
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
    {0x1b6804, {0xf0,0xb5,0x04,0x46,0x83,0xb0,0x09,0xb1}, static_object, &jsr_coli60_static_object_tail},
    {0x1bb5f4, {0x70,0xb4,0xa0,0xb1,0x0b,0x4b,0x01,0x46}, enemy_pos, &jsr_coli60_enemy_pos_tail},
    {0x1b63e4, {0x2d,0xe9,0xf0,0x41,0x16,0x46,0x04,0x46}, simple_move, &jsr_coli60_simple_move_tail},
    {0x1b662c, {0xf0,0xb5,0x04,0x46,0x83,0xb0,0x09,0xb1}, ground, &jsr_coli60_ground_tail},
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
  memcpy(&jsr_coli60_enemy_pos_literal, (uint8_t *)module->load_base + 0x1bb628, 4);
  memcpy(&g_original_free, (uint8_t *)module->load_base + 0x2a8dcc, 4);
  if (!g_original_free)
    return 0;
  g_matrix = (float **)(base + 0x63d838);
  g_result_pool = (const uint32_t *)(base + 0x3d8bd8);
  g_camera_callback = base + 0x1d90b5;
  for (unsigned index = 0; index < sizeof hooks / sizeof hooks[0]; index++) {
    uint32_t stub[] = {0xf000f8df, (uint32_t)(uintptr_t)hooks[index].replacement};
    memcpy((uint8_t *)module->load_base + hooks[index].offset, stub, sizeof stub);
    *hooks[index].tail = (base + hooks[index].offset + 8) | 1u;
  }
  uint32_t free_replacement = (uint32_t)(uintptr_t)game_free;
  memcpy((uint8_t *)module->load_base + 0x2a8dcc, &free_replacement, sizeof free_replacement);
  jsr_collision60_reset();
  g_installed = 1;
  debugPrintf("[native60] collision and graffiti queries renewed every engine frame; live-to-replay cache retained\n");
  return 1;
}
