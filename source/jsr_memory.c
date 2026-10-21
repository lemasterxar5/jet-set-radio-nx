#include <stdint.h>
#include <switch.h>

#include "jsr_memory.h"
#include "bionic.h"
#include "error.h"
#include "imports.h"
#include "util.h"

static VirtmemReservation *g_arena_reservation;
extern void *b_mmap(void *, size_t, int, int, int, b_off_t);

static void *game_mmap(void *address, size_t length, int protection, int flags, int descriptor, b_off_t offset) {
  void *mapped = b_mmap(address, length, protection, flags, descriptor, offset);
  if (!dcr_is_emulator() && address == (void *)0x20000000 && length == 0x01000000 && mapped != address) {
    fatal_error("The game's fixed memory window could not be mapped.\nRestart the application.\nSee debug.log for the mapping error.");
    return L_MAP_FAILED;
  }
  return mapped;
}

const DynLibFunction port_imports[] = {{"mmap", (uintptr_t)game_mmap}};
const int port_imports_count = sizeof port_imports / sizeof port_imports[0];

int jsr_memory_prepare(void) {
  if (dcr_is_emulator() || g_arena_reservation)
    return 1;
  const uintptr_t start = 0x20000000u;
  const size_t size = 0x01000000u;
  MemoryInfo info;
  u32 page;
  virtmemLock();
  Result result = svcQueryMemory(&info, &page, start);
  if (R_FAILED(result) || info.type != MemType_Unmapped ||
      info.addr > start || (uint64_t)info.addr + info.size < (uint64_t)start + size) {
    virtmemUnlock();
    debugPrintf("[memory] fixed game arena 0x20000000..0x21000000 unavailable (query 0x%x)\n", (unsigned)result);
    return 0;
  }
  g_arena_reservation = virtmemAddReservation((void *)start, size);
  virtmemUnlock();
  if (!g_arena_reservation) {
    debugPrintf("[memory] could not reserve the game's fixed arena\n");
    return 0;
  }
  debugPrintf("[memory] reserved 0x20000000..0x21000000 before loading the game; code and aliases excluded\n");
  return 1;
}
