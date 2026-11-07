"""Compile the real publisher against a deterministic host-side Horizon stub."""
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

STUB = r'''
#ifndef TEST_SWITCH_H
#define TEST_SWITCH_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef uint32_t Result;
typedef uint32_t Handle;
typedef uint64_t u64;
typedef int64_t s64;
typedef int Mutex;
typedef struct { void (*entry)(void *); void *arg; } Thread;
typedef struct { bool signaled; } UEvent;
typedef int Waiter;
#define INVALID_HANDLE 0
#define InfoType_ResourceLimit 9
#define LimitableResource_Sessions 4
#define CUR_THREAD_HANDLE 0xffff8000
void mutexLock(Mutex *);
void mutexUnlock(Mutex *);
Result svcGetInfo(uint64_t *, uint32_t, Handle, uint64_t);
Result svcGetResourceLimitCurrentValue(int64_t *, Handle, uint32_t);
Result svcGetResourceLimitLimitValue(int64_t *, Handle, uint32_t);
void smExit(void);
Result smInitialize(void);
Result threadCreate(Thread *, void (*)(void *), void *, void *, size_t, int, int);
Result threadStart(Thread *);
Result threadWaitForExit(Thread *);
Result threadClose(Thread *);
void ueventCreate(UEvent *, bool);
void ueventSignal(UEvent *);
Waiter waiterForUEvent(UEvent *);
Result waitSingle(Waiter, uint64_t);
typedef struct { Handle session; } Service;
typedef struct { Handle handle; size_t size; void *map_addr; } SharedMemory;
typedef struct { bool in_send_pid; int out_handle_attrs[1]; Handle *out_handles; } SfDispatchParams;
#define Perm_Rw 3
#define SfOutHandleAttr_HipcCopy 1
#define R_FAILED(r) ((r) != 0)
#define R_SUCCEEDED(r) ((r) == 0)
uint64_t armGetSystemTick(void);
Result svcConnectToNamedPort(Handle *, const char *);
Result svcCloseHandle(Handle);
void shmemLoadRemote(SharedMemory *, Handle, size_t, int);
Result shmemMap(SharedMemory *);
Result shmemClose(SharedMemory *);
Result fake_dispatch(Service *, int, const void *, size_t, void *, size_t, SfDispatchParams);
#define serviceDispatch(s,c,...) fake_dispatch(s,c,NULL,0,NULL,0,(SfDispatchParams){__VA_ARGS__})
#define serviceDispatchIn(s,c,i,...) fake_dispatch(s,c,&(i),sizeof(i),NULL,0,(SfDispatchParams){__VA_ARGS__})
#define serviceDispatchInOut(s,c,i,o,...) fake_dispatch(s,c,&(i),sizeof(i),&(o),sizeof(o),(SfDispatchParams){__VA_ARGS__})
#endif
'''

FIXTURE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "switch.h"
void debugPrintf(const char *format, ...) {(void)format;}
#include "jsr_saltynx.c"
static unsigned char page[4096 + 16];
static uint64_t clock_ticks;
static int mode, connects, closes, maps, unmaps, reserves, commands;
static int locks, creates, starts, joins, thread_closes, sm_released, sm_exits, sm_inits;
static Thread *started_thread;
static int waits;
void mutexLock(Mutex *m) {(void)m; assert(locks++ == 0);}
void mutexUnlock(Mutex *m) {(void)m; assert(--locks == 0);}
Result svcGetInfo(uint64_t *value, uint32_t id, Handle h, uint64_t sub) {
    (void)id; (void)h; (void)sub; *value = 300; return mode == 11 ? 1 : 0;
}
Result svcGetResourceLimitCurrentValue(int64_t *value, Handle h, uint32_t resource) {
    (void)resource; assert(h == 300); *value = mode == 10 ? 4 : 1; return 0;
}
Result svcGetResourceLimitLimitValue(int64_t *value, Handle h, uint32_t resource) {
    (void)resource; assert(h == 300); *value = 4; return 0;
}
void smExit(void) {assert(!sm_released); sm_released = 1; sm_exits++;}
Result smInitialize(void) {assert(sm_released); sm_released = 0; sm_inits++; return 0;}
Result threadCreate(Thread *t, void (*fn)(void *), void *arg, void *stack, size_t size, int priority, int core) {
    (void)stack; (void)priority; (void)core; assert(size >= 0x4000); creates++;
    t->entry = fn; t->arg = arg; return mode == 8 ? 1 : 0;
}
Result threadStart(Thread *t) {assert(t->entry); started_thread = t; starts++; return mode == 9 ? 1 : 0;}
Result threadWaitForExit(Thread *t) {assert(t->entry); joins++; return 0;}
Result threadClose(Thread *t) {assert(t->entry); thread_closes++; return 0;}
void ueventCreate(UEvent *e, bool clear) {(void)clear; e->signaled = false;}
void ueventSignal(UEvent *e) {e->signaled = true;}
Waiter waiterForUEvent(UEvent *e) {(void)e; return 1;}
Result waitSingle(Waiter w, uint64_t timeout) {
    (void)w; assert(timeout == 50000000); waits++;
    __atomic_store_n(&g_stopping, 1, __ATOMIC_RELEASE); return 0;
}
uint64_t armGetSystemTick(void) { return clock_ticks; }
Result svcConnectToNamedPort(Handle *h, const char *name) {
    assert(strcmp(name, "SaltySD") == 0); ++connects;
    if (mode == 1 || (mode == 10 && !sm_released)) return 1;
    *h = 100; return 0;
}
Result svcCloseHandle(Handle h) { assert(h == 100 || h == 200 || h == 300); if (h != 300) ++closes; return 0; }
void shmemLoadRemote(SharedMemory *s, Handle h, size_t size, int perm) {
    assert(h == 200 && size == 4096 && perm == Perm_Rw);
    s->handle = h; s->size = size; s->map_addr = NULL;
}
Result shmemMap(SharedMemory *s) {
    ++maps; if (mode == 3) return 1; s->map_addr = page; return 0;
}
Result shmemClose(SharedMemory *s) {
    if (s->map_addr) ++unmaps;
    if (s->handle) svcCloseHandle(s->handle);
    memset(s, 0, sizeof(*s)); return 0;
}
Result fake_dispatch(Service *s, int c, const void *in, size_t ni,
                     void *out, size_t no, SfDispatchParams p) {
    assert(s->session == 100 && p.in_send_pid); ++commands;
    if (c == 0) { assert(ni == 8); return 0; }
    if (c == 7) {
        if (mode == 2) return 1;
        assert(p.out_handle_attrs[0] == SfOutHandleAttr_HipcCopy);
        *p.out_handles = 200; return 0;
    }
    assert(c == 6 && ni == 8 && no == 8);
    assert(*(const uint64_t *)in == 174); ++reserves;
    if (mode == 4) return 1;
    *(uint64_t *)out = mode == 5 ? 4092 : (mode == 6 ? 0 : (mode == 7 ? 3 : 4));
    return 0;
}
static uint32_t u32(size_t offset) { uint32_t v; memcpy(&v, page + 4 + offset, 4); return v; }
static uint64_t read_u64(size_t offset) { uint64_t v; memcpy(&v, page + 4 + offset, 8); return v; }
static uint16_t u16(size_t offset) { uint16_t v; memcpy(&v, page + 4 + offset, 2); return v; }
static float f32(size_t offset) { float v; memcpy(&v, page + 4 + offset, 4); return v; }
static void reset(int failure) {
    jsr_saltynx_exit(); memset(page, 0, sizeof(page));
    memset(page + 4096, 0xa5, 16);
    clock_ticks = 0; connects = closes = maps = unmaps = reserves = commands = 0; mode = failure;
    creates = starts = joins = thread_closes = sm_exits = sm_inits = 0;
    assert(!locks && !sm_released);
}
static void canary(void) { for (int i=4096; i<4112; ++i) assert(page[i] == 0xa5); }
static void frame(unsigned width, unsigned height) {
    jsr_saltynx_present(width, height);
    publisher_step();
}
#define jsr_saltynx_present(w,h) frame(w,h)
int main(void) {
    reset(10); jsr_saltynx_init();
    assert(sm_exits == 1 && sm_inits == 1 && !sm_released);
    assert(creates == 1 && starts == 1);
    jsr_saltynx_exit(); assert(joins == 1 && thread_closes == 1);
    reset(11); jsr_saltynx_init(); assert(sm_exits == 0 && sm_inits == 0);
    jsr_saltynx_init(); assert(creates == 1 && connects == 1);
    for (int failure = 8; failure <= 9; ++failure) {
        reset(failure); jsr_saltynx_init();
        assert(creates == 1 && starts == (failure == 9));
        assert(unmaps == 1 && thread_closes == (failure == 9));
        jsr_saltynx_exit(); assert(joins == 0);
    }
    reset(1); jsr_saltynx_init();
    for (int i=0;i<1000;++i) (jsr_saltynx_present)(1280,720);
    assert(connects == 1 && commands == 0);
    clock_ticks = 19200000; (jsr_saltynx_present)(1280,720); assert(connects == 1);
    publisher_step(); assert(connects == 2);
    for (int failure=2;failure<=5;++failure) {
        reset(failure); jsr_saltynx_init(); jsr_saltynx_exit();
        assert(connects == 1 && closes == (failure == 2 ? 1 : 2));
        assert(unmaps == (failure >= 4 ? 1 : 0)); canary();
    }
    reset(6); jsr_saltynx_init(); assert(read_magic(page) == 0x465053); canary();
    memset(page+4, 0, 4096-4); /* SaltyNX cleanAppData preserves offset-zero magic. */
    jsr_saltynx_present(1280,720);
    assert(page[14] == 2 && page[9] == 1 && read_magic(page) == 0x465053);
    reset(7); jsr_saltynx_init(); assert(unmaps == 1); canary();
    reset(0); jsr_saltynx_init(); assert(reserves == 1 && u32(0) == 0x465053);
    assert(page[4+14] == 2);
    jsr_saltynx_present(1280,720);
    for (int i=0;i<60;++i) { clock_ticks += 320000; jsr_saltynx_present(1280,720); }
    assert(page[4+4] == 60 && f32(5) > 59.99f && f32(5) < 60.01f);
    for (int i=0;i<10;++i) assert(u32(15+i*4) == 320000);
    assert(read_u64(165) == 61 && page[4+9] == 1);
    page[4+9] = 0; publisher_step();
    assert(page[4+9] == 1 && read_u64(165) == 61);
    clock_ticks += 19200001; publisher_step();
    assert(page[4+4] == 0 && f32(5) == 0 && read_u64(165) == 61);
    page[4+9] = 0; /* Overlay consumes this heartbeat. */
    page[4+60+4] = 255; page[4+60+5] = 255;
    jsr_saltynx_record_viewport(640,360); jsr_saltynx_record_viewport(1280,720);
    jsr_saltynx_record_viewport(640,360);
    clock_ticks += 320000; jsr_saltynx_present(1920,1080);
    assert(page[4+9] == 1 && u16(60+4) == 0);
    assert(u16(108) == 640 && u16(110) == 360 && u16(112) == 2);
    assert(u16(114) == 1280 && u16(116) == 720 && u16(118) == 1);
    clock_ticks += 320000; jsr_saltynx_present(1920,1080);
    assert(u16(108) == 1920 && u16(110) == 1080 && u16(112) == 1);
    for (int i=0;i<70000;++i) jsr_saltynx_record_viewport(640,360);
    clock_ticks += 320000; jsr_saltynx_present(1280,720); assert(u16(112) == 65534);
    jsr_saltynx_record_viewport(0,720); jsr_saltynx_record_viewport(70000,720);
    clock_ticks += 320000; jsr_saltynx_present(0,0); assert(u16(112) == 0);
    int old_commands=commands; jsr_saltynx_exit(); assert(page[4+9] == 0);
    assert(unmaps == 1); jsr_saltynx_init(); assert(reserves == 1);
    assert(commands == old_commands + 2); /* handle, end-session; exit is IPC-free */
    jsr_saltynx_present(1280,720);
    for (int i=0;i<30;++i) { clock_ticks += 640000; jsr_saltynx_present(1280,720); }
    assert(page[4+4] == 30 && f32(5) > 29.99f && f32(5) < 30.01f);
    clock_ticks = 1; jsr_saltynx_present(1280,720); assert(f32(5) == 0);
    clock_ticks += 320000; jsr_saltynx_present(1280,720); assert(f32(5) == 60);
    for (unsigned i=0;i<10;++i) jsr_saltynx_record_viewport(640+i,360);
    clock_ticks += 320000; jsr_saltynx_present(1280,720);
    for (unsigned i=0;i<8;++i) assert(u16(108+i*6) == 640+i);
    jsr_saltynx_present(1280,720); assert(f32(5) == 0); /* Identical timestamp. */
    clock_ticks += (uint64_t)UINT32_MAX + 1;
    jsr_saltynx_present(1280,720); assert(f32(5) == 0);
    memset(page+4,0,4); mode=1;
    jsr_saltynx_present(1280,720); assert(unmaps == 2);
    clock_ticks += 19200000; mode=0; jsr_saltynx_present(1280,720);
    assert(reserves == 2 && u32(0) == 0x465053);
    waits = 0; started_thread->entry(started_thread->arg); assert(waits == 1);
    canary(); jsr_saltynx_exit();
    assert(locks == 0);
    puts("SaltyNX ABI, early session recovery, independent worker publication, measured FPS, viewports and shutdown: PASS");
    return 0;
}
'''


class SaltyNXTests(unittest.TestCase):
    def test_real_publisher_with_mock_horizon(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        self.assertIsNotNone(compiler, "A host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="jsr-saltynx-", dir=ROOT / "tests") as directory:
            temp = pathlib.Path(directory)
            (temp / "switch.h").write_text(STUB, encoding="utf-8")
            (temp / "util.h").write_text('void debugPrintf(const char *format, ...);\n', encoding="utf-8")
            (temp / "fixture.c").write_text(FIXTURE, encoding="utf-8")
            executable = temp / "fixture.exe"
            coverage = os.environ.get("JSR_TEST_COVERAGE") == "1"
            flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if coverage else []
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I", str(temp), "-I", str(ROOT / "source"),
                            str(temp / "fixture.c"), "-o", str(executable)], check=True)
            env = dict(os.environ, LLVM_PROFILE_FILE=str(temp / "test.profraw"))
            subprocess.run([str(executable)], check=True, env=env)
            if coverage:
                bindir = pathlib.Path(compiler).parent
                profdata = bindir / "llvm-profdata.exe"
                cov = bindir / "llvm-cov.exe"
                profile = temp / "test.profdata"
                subprocess.run([str(profdata), "merge", "-sparse", str(temp / "test.profraw"),
                                "-o", str(profile)], check=True)
                report = subprocess.check_output([str(cov), "export", str(executable),
                    "-instr-profile=" + str(profile), str(ROOT / "source/jsr_saltynx.c")], text=True)
                summaries = json.loads(report)["data"][0]["files"]
                summary = next(item["summary"] for item in summaries
                               if item["filename"].replace("\\", "/").endswith("/jsr_saltynx.c"))
                percent = summary["lines"]["percent"]
                print(f"SaltyNX production line coverage: {percent:.2f}%")
                self.assertGreaterEqual(percent, 80)


if __name__ == "__main__":
    unittest.main()

