#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "jsr_event60.h"

float jsr_event60_scale = 1.0f;
unsigned jsr_event60_ticks = 1;
uintptr_t jsr_event60_sound_skip;
uintptr_t jsr_event60_camera0_tail;
extern void jsr_event60_camera0(void);
uintptr_t jsr_event60_camera1_tail;
extern void jsr_event60_camera1(void);
uintptr_t jsr_event60_camera_reverse_tail;
extern void jsr_event60_camera_reverse(void);
uintptr_t jsr_event60_enemy_root_tail;
extern void jsr_event60_enemy_root(void);
uintptr_t jsr_event60_enemy_pose_tail;
extern void jsr_event60_enemy_pose(void);
uintptr_t jsr_event60_enemy_clip0_tail;
extern void jsr_event60_enemy_clip0(void);
uintptr_t jsr_event60_enemy_clip1_tail;
extern void jsr_event60_enemy_clip1(void);
uintptr_t jsr_event60_enemy_clip2_tail;
extern void jsr_event60_enemy_clip2(void);
uintptr_t jsr_event60_object_root_tail;
extern void jsr_event60_object_root(void);
uintptr_t jsr_event60_object_pose0_tail;
extern void jsr_event60_object_pose0(void);
uintptr_t jsr_event60_object_pose1_tail;
extern void jsr_event60_object_pose1(void);
uintptr_t jsr_event60_object_pose2_tail;
extern void jsr_event60_object_pose2(void);
uintptr_t jsr_event60_object_history_tail;
extern void jsr_event60_object_history(void);
uintptr_t jsr_event60_player_root_tail;
extern void jsr_event60_player_root(void);
uintptr_t jsr_event60_player_pose_tail;
extern void jsr_event60_player_pose(void);
uintptr_t jsr_event60_player_clip_tail;
extern void jsr_event60_player_clip(void);
uintptr_t jsr_event60_player_blend_tail;
extern void jsr_event60_player_blend(void);
uintptr_t jsr_event60_rival_root0_tail;
extern void jsr_event60_rival_root0(void);
uintptr_t jsr_event60_rival_clip_tail;
extern void jsr_event60_rival_clip(void);
uintptr_t jsr_event60_rival_pose_tail;
extern void jsr_event60_rival_pose(void);
uintptr_t jsr_event60_rival_root1_tail;
extern void jsr_event60_rival_root1(void);
uintptr_t jsr_event60_rival_reverse_tail;
extern void jsr_event60_rival_reverse(void);
uintptr_t jsr_event60_player_sound_tail;
extern void jsr_event60_player_sound(void);

static uintptr_t g_base;
static const struct {
  uintptr_t offset;
  uint8_t prefix[8];
  unsigned sound, timer;
} g_callbacks[] = {
    {0x163e64, {0xf0,0xb5,0xd0,0xf8,0xd8,0x70,0x8d,0xb0}, 0, 0},
    {0x1645ac, {0xf0,0xb5,0x04,0x46,0x85,0xb0,0x00,0x20}, 188, 192},
    {0x16500c, {0x2d,0xe9,0xf0,0x47,0x04,0x46,0x00,0x20}, 184, 188},
    {0x165654, {0xf0,0xb5,0x04,0x46,0x91,0xb0,0x00,0x20}, 220, 224},
    {0x16605c, {0xf0,0xb5,0x04,0x46,0x91,0xb0,0x00,0x20}, 204, 208},
};
static const struct {
  uintptr_t offset;
  uint8_t prefix[16];
  unsigned length;
  void *replacement;
  uintptr_t *tail;
} g_hooks[] = {
    {0x164092, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xc4,0xed,0x1f,0x7a}, 12, jsr_event60_camera0, &jsr_event60_camera0_tail},
    {0x1640ea, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xc4,0xed,0x1f,0x7a}, 12, jsr_event60_camera1, &jsr_event60_camera1_tail},
    {0x16414c, {0xf7,0xee,0x00,0x7a,0x77,0xee,0x67,0x7a}, 8, jsr_event60_camera_reverse, &jsr_event60_camera_reverse_tail},
    {0x164600, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_enemy_root, &jsr_event60_enemy_root_tail},
    {0x164650, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_enemy_pose, &jsr_event60_enemy_pose_tail},
    {0x16468e, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xc4,0xed,0x2d,0x7a}, 12, jsr_event60_enemy_clip0, &jsr_event60_enemy_clip0_tail},
    {0x16472c, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_enemy_clip1, &jsr_event60_enemy_clip1_tail},
    {0x164760, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_enemy_clip2, &jsr_event60_enemy_clip2_tail},
    {0x165060, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_object_root, &jsr_event60_object_root_tail},
    {0x165158, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_object_pose0, &jsr_event60_object_pose0_tail},
    {0x1651ca, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xc4,0xed,0x2c,0x7a}, 12, jsr_event60_object_pose1, &jsr_event60_object_pose1_tail},
    {0x1651fe, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xe3,0x6e,0xc4,0xed,0x2c,0x7a}, 14, jsr_event60_object_pose2, &jsr_event60_object_pose2_tail},
    {0x1650b0, {0xf7,0xee,0x00,0x7a,0x94,0xed,0x31,0x7a,0x76,0xee,0xa7,0x7a}, 12, jsr_event60_object_history, &jsr_event60_object_history_tail},
    {0x1656a8, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_player_root, &jsr_event60_player_root_tail},
    {0x1656f0, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_player_pose, &jsr_event60_player_pose_tail},
    {0x165748, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_player_clip, &jsr_event60_player_clip_tail},
    {0x16575e, {0x76,0xee,0x87,0x6a,0xc4,0xed,0x35,0x6a,0x5b,0x88}, 10, jsr_event60_player_blend, &jsr_event60_player_blend_tail},
    {0x1660c4, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a}, 8, jsr_event60_rival_root0, &jsr_event60_rival_root0_tail},
    {0x1660fa, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xd4,0xf8,0xcc,0x00}, 12, jsr_event60_rival_clip, &jsr_event60_rival_clip_tail},
    {0x166126, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xc4,0xed,0x4a,0x7a}, 12, jsr_event60_rival_pose, &jsr_event60_rival_pose_tail},
    {0x166162, {0xb7,0xee,0x00,0x7a,0x77,0xee,0x87,0x7a,0xc4,0xed,0x31,0x7a}, 12, jsr_event60_rival_root1, &jsr_event60_rival_root1_tail},
    {0x16637a, {0xf7,0xee,0x00,0x7a,0x77,0xee,0x67,0x7a,0xf5,0xee,0xc0,0x7a}, 12, jsr_event60_rival_reverse, &jsr_event60_rival_reverse_tail},
    {0x165792, {0x4f,0xf0,0x0a,0x0e,0xcd,0xf8,0x00,0xe0,0x07,0x20}, 10, jsr_event60_player_sound, &jsr_event60_player_sound_tail},
};

int jsr_event60_validate(so_module *module) {
  if (module->state != SO_STAGED)
    return 0;
  for (unsigned index = 0; index < sizeof g_callbacks / sizeof g_callbacks[0]; index++) {
    uintptr_t offset = g_callbacks[index].offset;
    if (offset > module->load_size || 8 > module->load_size - offset ||
        memcmp((uint8_t *)module->load_base + offset, g_callbacks[index].prefix, 8))
      return 0;
  }
  for (unsigned index = 0; index < sizeof g_hooks / sizeof g_hooks[0]; index++) {
    uintptr_t offset = g_hooks[index].offset;
    unsigned length = g_hooks[index].length;
    if (offset > module->load_size || length > module->load_size - offset ||
        memcmp((uint8_t *)module->load_base + offset, g_hooks[index].prefix, length))
      return 0;
  }
  return 1;
}

void jsr_event60_install(so_module *module) {
  g_base = (uintptr_t)module->load_virtbase;
  for (unsigned index = 0; index < sizeof g_hooks / sizeof g_hooks[0]; index++) {
    uintptr_t offset = g_hooks[index].offset;
    uint8_t *alias = (uint8_t *)module->load_base + offset;
    uint16_t instruction[] = {0xf8df, 0xf000};
    unsigned literal = (offset & 2) ? 6 : 4;
    uint32_t replacement = (uint32_t)(uintptr_t)g_hooks[index].replacement;
    if (literal == 6)
      instruction[1] |= 4;
    memcpy(alias, instruction, sizeof instruction);
    memcpy(alias + literal, &replacement, sizeof replacement);
    *g_hooks[index].tail = (g_base + offset + g_hooks[index].length) | 1u;
  }
  jsr_event60_sound_skip = g_base + 0x1657ae + 1;
}

int jsr_event60_dispatch(void *object, void (*callback)(void *), float scale, unsigned ticks) {
  for (unsigned index = 0; index < sizeof g_callbacks / sizeof g_callbacks[0]; index++) {
    if ((uintptr_t)callback != g_base + g_callbacks[index].offset + 1)
      continue;
    unsigned timer = g_callbacks[index].timer;
    if (timer) {
      uint32_t sound;
      int32_t remaining;
      memcpy(&sound, (uint8_t *)object + g_callbacks[index].sound, sizeof sound);
      memcpy(&remaining, (uint8_t *)object + timer, sizeof remaining);
      if (sound && remaining >= 0 && remaining < INT_MAX) {
        remaining += 1 - (int32_t)ticks;
        memcpy((uint8_t *)object + timer, &remaining, sizeof remaining);
      }
    }
    float previous_scale = jsr_event60_scale;
    unsigned previous_ticks = jsr_event60_ticks;
    jsr_event60_scale = scale;
    jsr_event60_ticks = ticks;
    callback(object);
    jsr_event60_scale = previous_scale;
    jsr_event60_ticks = previous_ticks;
    return 1;
  }
  return 0;
}

