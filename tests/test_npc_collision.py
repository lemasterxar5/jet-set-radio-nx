from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root.parent / 'test-deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
import unicorn.arm_const as registers

source = (root / 'source/jsr_collision60.c').read_text()
prefix = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {void *load_base, *load_virtbase; size_t load_size; int state;} so_module;
#define SO_STAGED 0
typedef int Mutex;
static unsigned lock_depth, bad;
void mutexLock(Mutex *lock) {(void)lock; if (lock_depth++) bad++;}
void mutexUnlock(Mutex *lock) {(void)lock; if (!lock_depth--) bad++;}
void debugPrintf(const char *format, ...) {(void)format;}
'''
suffix = r'''
static unsigned calls[4], freed;
static float expected[4] = {1,2,3,4};
static void *expected_collider, *expected_result;
void jsr_coli60_original_ride(void *a,const float *b,const float *c,void *d,uint32_t e,uint32_t f) {}
void jsr_coli60_original_move(void *a,const float *b,const float *c,const float *d,float e,void *f,uint32_t g,uint32_t h) {}
void jsr_coli60_original_object(void *a,void *b,float c,void *d,uint32_t e,void *f) {}
void jsr_coli60_original_graffiti_pos(const float *a,void *b) {}
void jsr_coli60_original_graffiti_ignore(const float *a,void *b) {}
void jsr_coli60_original_graffiti_range(void *a,void *b) {}
void jsr_coli60_original_graffiti_enemy_range(const float *a,void *b,void *c) {}
void jsr_coli60_original_follow(void *a,void *b) {}
void jsr_coli60_original_static_object(void *collider,void *result,uint32_t owner) {
  if (collider != expected_collider || result != expected_result || owner != 106) bad++;
  calls[0]++;
}
void jsr_coli60_original_simple_move(void *collider,const float *sphere,void *result,uint32_t owner,uint32_t flags) {
  if (collider != expected_collider || result != expected_result || owner != 106 || flags != 9 || memcmp(sphere,expected,16)) bad++;
  calls[1]++;
}
void jsr_coli60_original_ground(void *collider,void *result,uint32_t owner) {
  if (collider != expected_collider || result != expected_result || owner != 106) bad++;
  calls[2]++;
}
void jsr_coli60_original_enemy_pos(const float *position) {
  if (memcmp(position,expected,12)) bad++;
  calls[3]++;
}
static void fixture_free(void *pointer) {(void)pointer; if (lock_depth != 1) bad++; freed++;}
int main(void) {
  uint32_t actor[12] = {0};
  float sphere[4], matrix[16] = {0}, *matrix_pointer = matrix;
  expected_collider = sphere;
  expected_result = actor + 10;
  g_installed = 1;
  g_matrix = &matrix_pointer;
  g_original_free = fixture_free;
  memcpy(sphere, expected, 16);
  jsr_collision60_begin_npc(actor);
  static_object(sphere, expected_result, 106);
  simple_move(sphere, sphere, expected_result, 106, 9);
  ground(sphere, expected_result, 106);
  enemy_pos(sphere);
  jsr_collision60_end();
  memset(sphere, 0, 16);
  game_free(NULL);
  if (freed) return 8;
  if (!jsr_collision60_hold(actor)) return 1;
  for (unsigned index=0; index<4; index++) if (calls[index] != 2) return 2;
  game_free(sphere);
  if (jsr_collision60_hold(actor) || freed != 1) return 3;
  memcpy(sphere, expected, 16);
  jsr_collision60_begin_npc(actor);
  static_object(sphere, expected_result, 106);
  jsr_collision60_end();
  actor[8 / 4] = 1;
  if (jsr_collision60_hold(actor)) return 4;
  actor[8 / 4] = 0;
  jsr_collision60_begin_npc(actor);
  static_object(sphere, expected_result, 106);
  jsr_collision60_end();
  game_free(expected_result);
  if (jsr_collision60_hold(actor) || freed != 2) return 9;
  jsr_collision60_begin_npc(actor);
  jsr_collision60_end();
  game_free(actor);
  if (jsr_collision60_hold(actor)) return 5;
  uint32_t pool[54] = {0};
  g_result_pool = pool;
  CollisionActor *entry = find_actor(actor, 0);
  entry->count = 1;
  entry->queries[0].kind = 0;
  entry->queries[0].result = (void *)0xdeadbeef;
  jsr_collision60_prepare_npc(actor);
  entry->valid = 1;
  actor[8 / 4]++;
  jsr_collision60_prepare_npc(actor);
  if (entry->valid) return 7;
  jsr_collision60_reset();
  if (g_actor_count || lock_depth || bad) return 6;
  return 0;
}
'''

def load_elf(machine, blob, base=0):
    start = struct.unpack_from('<I', blob, 28)[0]
    stride, count = struct.unpack_from('<HH', blob, 42)
    for index in range(count):
        kind, offset, address, _, size, memory, _, _ = struct.unpack_from('<8I', blob, start + index * stride)
        if kind == 1:
            machine.mem_write(base + address, bytes(memory))
            machine.mem_write(base + address, blob[offset:offset + size])

compiler = 'C:/devkitPro/devkitARM/bin/arm-none-eabi-gcc.exe'
with tempfile.TemporaryDirectory(dir=root / 'tests') as directory:
    temporary = Path(directory)
    code = prefix + '\n'.join(line for line in source.splitlines() if not line.startswith('#include')) + suffix
    (temporary / 'test.c').write_text(code)
    subprocess.run([compiler, '-O2', '-Wno-unused-parameter', '-march=armv7-a', '-mfloat-abi=soft',
                    '-nostartfiles', '-Wl,-Ttext=0x10000', '-Wl,-e,main', str(temporary / 'test.c'),
                    '-specs=nosys.specs', '-o', str(temporary / 'test.elf')], check=True)
    blob = (temporary / 'test.elf').read_bytes()
    machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    machine.mem_map(0, 0x4000000)
    load_elf(machine, blob)
    machine.reg_write(registers.UC_ARM_REG_SP, 0x3ff0000)
    machine.reg_write(registers.UC_ARM_REG_LR, 0x3ffe000)
    machine.emu_start(struct.unpack_from('<I', blob, 24)[0], 0x3ffe000, count=3000000)
    assert machine.reg_read(registers.UC_ARM_REG_PC) == 0x3ffe000
    assert machine.reg_read(registers.UC_ARM_REG_R0) == 0, machine.reg_read(registers.UC_ARM_REG_R0)
print('PASS: actual ARM cache: compact NPC header, static/move/ground/graffiti renewal, copied stack vectors, collider/result/action free invalidation, callback identity changes and balanced locks.')

game = (root.parent / 'libjetsetradio.so').read_bytes()
machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
machine.mem_map(0, 0x4000000)
load_elf(machine, game, 0x2000000)
actor = 0x3000000
results = []
for calls in [60, 30]:
    machine.mem_write(actor + 0x198, struct.pack('<I', 120))
    machine.reg_write(registers.UC_ARM_REG_R4, actor)
    for step in range(calls):
        machine.emu_start(0x2207e21, 0x2207e2e, count=6)
        assert machine.reg_read(registers.UC_ARM_REG_PC) == 0x2207e2e
    results.append(struct.unpack('<I', machine.mem_read(actor + 0x198, 4))[0])
assert results == [60, 90], results
print('PASS: REAL APK police _EKLibChaseTarget timer at +0x198 decrements twice as fast with 60 callbacks; original 30-callback cadence restores its one-second duration. Full stage AI/geometry is not executed.')

assembly = (root / 'source/jsr_collision60_tramp.S').read_text()
symbols = sorted(set(re.findall(r'jsr_coli60_[a-z_]+(?:tail|literal)\b', assembly + source)))
with tempfile.TemporaryDirectory(dir=root / 'tests') as directory:
    temporary = Path(directory)
    (temporary / 'globals.c').write_text('#include <stdint.h>\n' + '\n'.join(f'uintptr_t {name};' for name in symbols) + '\nint main(void) {return 0;}\n')
    executable = temporary / 'tramp.elf'
    subprocess.run([compiler, '-march=armv8-a', '-mfloat-abi=softfp', '-mfpu=neon', '-nostartfiles',
                    '-Wl,-Ttext=0x10000', '-Wl,-e,main', str(temporary / 'globals.c'),
                    str(root / 'source/jsr_collision60_tramp.S'), '-specs=nosys.specs', '-o', str(executable)], check=True)
    port = executable.read_bytes()
    names = {}
    for line in subprocess.check_output(['C:/devkitPro/devkitARM/bin/arm-none-eabi-nm.exe', str(executable)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            names[fields[2]] = int(fields[0], 16)

    def prefix_state(label, offset, trampoline, null):
        machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        machine.mem_map(0, 0x4000000)
        load_elf(machine, game, 0x2000000)
        load_elf(machine, port)
        for index in range(12):
            machine.reg_write(getattr(registers, f'UC_ARM_REG_R{index}'), 0x3000000 + 0x100 * index)
        machine.reg_write(registers.UC_ARM_REG_SP, 0x3ff0000)
        machine.reg_write(registers.UC_ARM_REG_LR, 0x3ffe001)
        if null is not None:
            machine.reg_write(getattr(registers, f'UC_ARM_REG_R{null}'), 0)
        machine.mem_write(names[f'jsr_coli60_{label}_tail'], struct.pack('<I', 0x2000000 + offset + 9))
        machine.mem_write(names['jsr_coli60_enemy_pos_literal'], bytes(machine.mem_read(0x21bb628, 4)))
        stop = 0x2000000 + offset + 8
        if null is not None:
            stop = 0x3ffe000 if label == 'enemy_pos' else stop + 4
        entry = names[f'jsr_coli60_original_{label}'] if trampoline else 0x2000001 + offset
        machine.emu_start(entry | 1, stop, count=100)
        assert machine.reg_read(registers.UC_ARM_REG_PC) == stop
        state = [machine.reg_read(getattr(registers, f'UC_ARM_REG_R{index}')) for index in range(12)]
        stack = machine.reg_read(registers.UC_ARM_REG_SP)
        return state + [stack, machine.reg_read(registers.UC_ARM_REG_CPSR) & 0xf8000000,
                        bytes(machine.mem_read(stack, 0x3ff0000 - stack))]

    for label, offset, null in [('static_object', 0x1b6804, 1), ('ground', 0x1b662c, 1),
                                ('simple_move', 0x1b63e4, None), ('enemy_pos', 0x1bb5f4, 0)]:
        assert prefix_state(label, offset, False, None) == prefix_state(label, offset, True, None), label
        if null is not None:
            assert prefix_state(label, offset, False, null) == prefix_state(label, offset, True, null), label
print('PASS: four new real-APK registration prefixes match trampoline registers, flags and stack for normal and optional-NULL branches.')
