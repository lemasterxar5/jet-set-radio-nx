from pathlib import Path
import re
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'source/jsr_npc30.c').read_text()
game = (root.parent / 'libjetsetradio.so').read_bytes()
headers = struct.unpack_from('<I', game, 28)[0]
stride, count = struct.unpack_from('<HH', game, 42)
segments = [struct.unpack_from('<8I', game, headers + index * stride) for index in range(count)]

def image_at(address, length):
    for kind, offset, virtual, _, size, *_ in segments:
        if kind == 1 and virtual <= address and address + length <= virtual + size:
            return game[offset + address - virtual:offset + address - virtual + length]
    raise AssertionError(hex(address))

profiles = re.findall(r'\{0x([0-9a-f]+), \{([^}]+)\}\}', source)
assert len(profiles) >= 50
for offset, signature in profiles:
    expected = bytes(int(value.strip(), 16) for value in signature.split(','))
    assert image_at(int(offset, 16), len(expected)) == expected

fixture = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef struct {void *load_base, *load_virtbase; size_t load_size; int state;} so_module;
#define SO_STAGED 0
static unsigned begins, ends, holds, prepares, forgotten, calls;
void debugPrintf(const char *format, ...) {(void)format;}
void jsr_collision60_begin_npc(void *object) {(void)object; begins++;}
void jsr_collision60_prepare_npc(void *object) {(void)object; prepares++;}
void jsr_collision60_end(void) {ends++;}
int jsr_collision60_hold(void *object) {(void)object; holds++; return 1;}
void jsr_collision60_forget(void *object) {(void)object; forgotten++;}
static void callback(void *object) {(void)object; calls++;}
'''
fixture += '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
fixture += r'''
int main(void) {
  unsigned char *staging = calloc(1, 0x280000);
  for (unsigned index = 0; index < sizeof g_profiles / sizeof g_profiles[0]; index++)
    memcpy(staging + g_profiles[index].offset, g_profiles[index].prefix, 8);
  so_module module = {staging, (void *)((uintptr_t)callback - g_profiles[0].offset - 1), 0x280000, SO_STAGED};
  staging[g_profiles[1].offset] ^= 1;
  if (jsr_npc30_validate(&module)) return 1;
  staging[g_profiles[1].offset] ^= 1;
  if (!jsr_npc30_validate(&module)) return 2;
  jsr_npc30_install(&module);
  for (unsigned index = 0; index < sizeof g_profiles / sizeof g_profiles[0]; index++)
    if (!jsr_npc30_handles((void (*)(void *))(g_base + g_profiles[index].offset + 1))) return 3;
  if (jsr_npc30_handles((void (*)(void *))(g_base + 0x165655))) return 4;
  uint32_t actor[16] = {0};
  for (unsigned frame = 0; frame < 600; frame++)
    if (!jsr_npc30_dispatch(actor, callback, frame & 1)) return 5;
  if (calls != 300 || begins != 300 || ends != 300 || holds != 300 || prepares != 300) return 6;
  actor[0] = 1;
  jsr_npc30_dispatch(actor, callback, 0);
  if (forgotten != 1 || calls != 300 || holds != 300) return 7;
  if (jsr_npc30_dispatch(actor, (void (*)(void *))(g_base + 0x1d71c9), 1)) return 8;
  free(staging);
  return 0;
}
'''
with tempfile.TemporaryDirectory(dir=root / 'tests') as directory:
    target = Path(directory)
    (target / 'test.c').write_text(fixture)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', str(target / 'test.c'), '-o', str(target / 'test.exe')], check=True)
    subprocess.run([str(target / 'test.exe')], check=True)
print(f'PASS: {len(profiles)} APK signatures; original NPC cadence 300/600, held collision renewal, deletion, and player/event exclusion.')
