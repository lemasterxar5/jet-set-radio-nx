#include "jsr_saltynx.h"
#include "util.h"

#include <switch.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Independently implemented publisher for the public Nx-FPS shared-memory ABI.
 * Never take packed member addresses: AArch32 cannot safely dereference its
 * unaligned floats, tick counters or 64-bit frame counter. */
#define NX_FPS_MAGIC UINT32_C(0x465053)
#define NX_FPS_PAGE_SIZE 4096u
#define NX_FPS_TICK_RATE UINT64_C(19200000)
#define NX_FPS_BINS 8u

typedef struct __attribute__((packed)) {
    uint16_t width, height, calls;
} ResolutionCalls;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t fps;
    float fps_average;
    uint8_t plugin_active, fps_locked, fps_mode, zero_sync, patch_applied, api;
    uint32_t ticks[10];
    uint8_t buffers, set_buffers, active_buffers, set_active_buffers, display_sync;
    ResolutionCalls render[NX_FPS_BINS], viewport[NX_FPS_BINS];
    uint8_t force_original_refresh_rate, dont_force_60_in_docked, force_suspend;
    uint8_t current_refresh_rate;
    float read_speed_per_second;
    uint8_t fps_locked_docked;
    uint64_t frame_number;
    int8_t expected_set_buffers;
} NxFpsBlock;

_Static_assert(sizeof(NxFpsBlock) == 174, "Nx-FPS ABI size");
_Static_assert(offsetof(NxFpsBlock, fps_average) == 5, "Nx-FPS average offset");
_Static_assert(offsetof(NxFpsBlock, ticks) == 15, "Nx-FPS ticks offset");
_Static_assert(offsetof(NxFpsBlock, render) == 60, "Nx-FPS depth offset");
_Static_assert(offsetof(NxFpsBlock, viewport) == 108, "Nx-FPS viewport offset");
_Static_assert(offsetof(NxFpsBlock, frame_number) == 165, "Nx-FPS frame offset");

static SharedMemory g_memory;
static volatile uint8_t *g_block;
static uint64_t g_next_retry, g_previous_tick, g_window_tick, g_frame_number;
static uint32_t g_ticks[10], g_window_frames;
static unsigned g_tick_index;
static int g_have_previous, g_attempted;
static ResolutionCalls g_viewports[NX_FPS_BINS];
static unsigned g_failure_stage;
static Result g_failure_result;
static int g_published, g_reported_clear, g_present_seen;
static Mutex g_capture_lock;
static Thread g_publisher;
static UEvent g_wake;
static uint32_t g_stopping;
static int g_running;
static uint8_t g_fps;
static float g_average;
static ResolutionCalls g_present_viewports[NX_FPS_BINS];

static int make_session_room(void) {
    uint64_t raw = 0;
    Result rc = svcGetInfo(&raw, InfoType_ResourceLimit, 0, 0);
    if (R_FAILED(rc)) return 0;
    Handle limit = (Handle)raw;
    int64_t current = 0, maximum = 0;
    Result a = svcGetResourceLimitCurrentValue(&current, limit, LimitableResource_Sessions);
    Result b = svcGetResourceLimitLimitValue(&maximum, limit, LimitableResource_Sessions);
    svcCloseHandle(limit);
    int full = R_SUCCEEDED(a) && R_SUCCEEDED(b) && current >= maximum;
    debugPrintf("[saltynx] session budget: current=%lld maximum=%lld query=0x%x/0x%x\n",
                (long long)current, (long long)maximum, (unsigned)a, (unsigned)b);
    return full;
}

static void report_failure(unsigned stage, const char *name, Result result, uint64_t detail) {
    if (g_failure_stage != stage || g_failure_result != result) {
        debugPrintf("[saltynx] %s failed: Result=0x%08x detail=%llu; retry in 1s\n",
                    name, (unsigned)result, (unsigned long long)detail);
        g_failure_stage = stage;
        g_failure_result = result;
    }
}

/* memcpy through an aligned scalar, then volatile byte stores, also prevents
 * the compiler combining a packed u64 write into an unaligned STRD. */
static void block_write(size_t offset, const void *value, size_t size) {
    const uint8_t *bytes = value;
    for (size_t i = 0; i < size; ++i) g_block[offset + i] = bytes[i];
}

static uint32_t read_magic(const volatile uint8_t *address) {
    uint8_t bytes[4];
    uint32_t result;
    for (unsigned i = 0; i < 4; ++i) bytes[i] = address[i];
    memcpy(&result, bytes, sizeof(result));
    return result;
}

static void reset_clock(void) {
    g_previous_tick = g_window_tick = g_frame_number = 0;
    g_window_frames = g_tick_index = 0;
    g_have_previous = 0;
    memset(g_ticks, 0, sizeof(g_ticks));
}

static void close_mapping(void) {
    g_block = NULL;
    g_published = 0;
    if (g_memory.handle) shmemClose(&g_memory);
    memset(&g_memory, 0, sizeof(g_memory));
}

static void close_session(Service *service) {
    uint64_t zero = 0;
    serviceDispatchIn(service, 0, zero, .in_send_pid = true);
    svcCloseHandle(service->session);
    service->session = 0;
}

static volatile uint8_t *find_block(volatile uint8_t *page) {
    for (size_t offset = 0; offset <= NX_FPS_PAGE_SIZE - sizeof(NxFpsBlock); offset += 4) {
        if (read_magic(page + offset) == NX_FPS_MAGIC) return page + offset;
    }
    return NULL;
}

static void attach(uint64_t now) {
    Service service = {0};
    Handle handle = 0;
    uint64_t size = sizeof(NxFpsBlock), offset = 0;
    g_attempted = 1;
    g_next_retry = now + NX_FPS_TICK_RATE;
    Result result = svcConnectToNamedPort(&service.session, "SaltySD");
    if (R_FAILED(result)) {
        report_failure(1, "connect SaltySD", result, 0);
        return;
    }

    result = serviceDispatch(&service, 7, .in_send_pid = true,
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = &handle);
    if (R_FAILED(result) || !handle) {
        report_failure(2, "command 7 shared-memory handle", result, handle);
        if (handle) svcCloseHandle(handle);
        close_session(&service);
        return;
    }
    shmemLoadRemote(&g_memory, handle, NX_FPS_PAGE_SIZE, Perm_Rw);
    result = shmemMap(&g_memory);
    if (R_FAILED(result)) {
        report_failure(3, "map shared memory", result, handle);
        close_session(&service);
        close_mapping();
        return;
    }
    volatile uint8_t *page = g_memory.map_addr;
    g_block = find_block(page);
    if (!g_block) {
        result = serviceDispatchInOut(&service, 6, size, offset, .in_send_pid = true);
        if (R_SUCCEEDED(result) && !(offset & 3) && offset <= NX_FPS_PAGE_SIZE - size) {
            g_block = page + (size_t)offset;
            uint8_t empty[sizeof(NxFpsBlock)] = {0};
            block_write(0, empty, sizeof(empty));
            uint32_t magic = NX_FPS_MAGIC;
            block_write(offsetof(NxFpsBlock, magic), &magic, sizeof(magic));
        } else {
            report_failure(4, "command 6 reservation/bounds", result, offset);
        }
    }
    close_session(&service);
    if (!g_block) { close_mapping(); return; }
    const uint8_t api = 2; /* OpenGL/EGL */
    block_write(offsetof(NxFpsBlock, api), &api, sizeof(api));
    g_failure_stage = 0;
    g_failure_result = 0;
    g_reported_clear = 0;
    debugPrintf("[saltynx] attached: offset=%u API=%u block=%u bytes\n",
                (unsigned)(g_block - page), (unsigned)api, (unsigned)sizeof(NxFpsBlock));
}

static void record_viewport(unsigned width, unsigned height) {
    /* Match Status Monitor's landscape-resolution filters; do not report tiny
     * offscreen UI/shadow targets as the game's rendering resolution. */
    if (!width || width > UINT16_MAX || height < 160 || height > 1440 ||
        width * 5u <= height * 3u || width * 5u >= height * 9u) return;
    for (unsigned i = 0; i < NX_FPS_BINS; ++i) {
        ResolutionCalls *entry = &g_viewports[i];
        if (!entry->calls || (entry->width == width && entry->height == height)) {
            entry->width = (uint16_t)width;
            entry->height = (uint16_t)height;
            if (entry->calls < UINT16_MAX - 1) ++entry->calls;
            return;
        }
    }
}

void jsr_saltynx_record_viewport(unsigned width, unsigned height) {
    if (!g_running) return;
    mutexLock(&g_capture_lock);
    record_viewport(width, height);
    mutexUnlock(&g_capture_lock);
}

static void capture_resolution(unsigned width, unsigned height) {
    if (!g_viewports[0].calls) record_viewport(width, height);
    for (unsigned i = 1; i < NX_FPS_BINS; ++i) {
        ResolutionCalls entry = g_viewports[i];
        unsigned j = i;
        while (j && g_viewports[j - 1].calls < entry.calls) {
            g_viewports[j] = g_viewports[j - 1];
            --j;
        }
        g_viewports[j] = entry;
    }
    /* No depth attachment hook exists here. Zero also acknowledges the overlay
     * request sentinel (render[0].calls == 0xffff), without inventing depth. */
    memcpy(g_present_viewports, g_viewports, sizeof(g_viewports));
}

static void publish_timing(uint64_t now) {
    float average = 0;
    uint8_t fps = 0;
    if (g_have_previous && (now <= g_previous_tick || now - g_previous_tick > UINT32_MAX)) {
        reset_clock();
    }
    if (!g_have_previous) {
        g_window_tick = now;
        g_have_previous = 1;
        g_fps = fps;
    } else {
        uint32_t delta = (uint32_t)(now - g_previous_tick);
        if (!g_ticks[0]) for (unsigned i = 0; i < 10; ++i) g_ticks[i] = delta;
        g_ticks[g_tick_index] = delta;
        g_tick_index = (g_tick_index + 1) % 10;
        uint64_t total = 0;
        for (unsigned i = 0; i < 10; ++i) total += g_ticks[i];
        average = (float)(NX_FPS_TICK_RATE * 10) / (float)total;
        ++g_window_frames;
        if (now - g_window_tick >= NX_FPS_TICK_RATE) {
            uint64_t measured = (g_window_frames * NX_FPS_TICK_RATE +
                (now - g_window_tick) / 2) / (now - g_window_tick);
            fps = measured > UINT8_MAX ? UINT8_MAX : (uint8_t)measured;
            g_fps = fps;
            g_window_tick = now;
            g_window_frames = 0;
        }
    }
    g_previous_tick = now;
    ++g_frame_number;
    g_average = average;
}

void jsr_saltynx_present(unsigned width, unsigned height) {
    if (!g_running) return;
    mutexLock(&g_capture_lock);
    if (!g_present_seen) {
        debugPrintf("[saltynx] first successful EGL present: surface=%ux%u\n", width, height);
        g_present_seen = 1;
    }
    publish_timing(armGetSystemTick());
    capture_resolution(width, height);
    memset(g_viewports, 0, sizeof(g_viewports));
    mutexUnlock(&g_capture_lock);
}

static void publisher_step(void) {
    uint64_t now = armGetSystemTick();
    if (g_block && read_magic(g_block) != NX_FPS_MAGIC) {
        debugPrintf("[saltynx] shared block magic cleared/changed; reconnecting\n");
        close_mapping();
    }
    if (!g_block && (!g_attempted || now >= g_next_retry)) attach(now);
    if (g_block) {
        const uint8_t api = 2;
        if (g_block[offsetof(NxFpsBlock, api)] != api && !g_reported_clear) {
            debugPrintf("[saltynx] shared API cleared/changed; restoring publisher fields\n");
            g_reported_clear = 1;
        }
        block_write(offsetof(NxFpsBlock, api), &api, sizeof(api));
        uint8_t fps;
        float average;
        uint64_t frames;
        uint32_t ticks[10];
        ResolutionCalls viewports[NX_FPS_BINS], empty[NX_FPS_BINS] = {{0}};
        mutexLock(&g_capture_lock);
        now = armGetSystemTick();
        int recent = g_have_previous && now >= g_previous_tick && now - g_previous_tick < NX_FPS_TICK_RATE;
        fps = recent ? g_fps : 0;
        average = recent ? g_average : 0;
        frames = g_frame_number;
        memcpy(ticks, g_ticks, sizeof(ticks));
        memcpy(viewports, g_present_viewports, sizeof(viewports));
        mutexUnlock(&g_capture_lock);
        if (!recent) memset(ticks, 0, sizeof(ticks));
        block_write(offsetof(NxFpsBlock, fps), &fps, sizeof(fps));
        block_write(offsetof(NxFpsBlock, fps_average), &average, sizeof(average));
        block_write(offsetof(NxFpsBlock, ticks), ticks, sizeof(ticks));
        block_write(offsetof(NxFpsBlock, frame_number), &frames, sizeof(frames));
        block_write(offsetof(NxFpsBlock, render), empty, sizeof(empty));
        block_write(offsetof(NxFpsBlock, viewport), viewports, sizeof(viewports));
        __atomic_thread_fence(__ATOMIC_RELEASE);
        /* The overlay clears this byte; refresh on every successful present. */
        g_block[offsetof(NxFpsBlock, plugin_active)] = frames != 0;
        if (frames && !g_published) {
            debugPrintf("[saltynx] first telemetry publish: frames=%llu heartbeat=1\n", (unsigned long long)frames);
            g_published = 1;
        }
    }
}

static void publisher_run(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&g_stopping, __ATOMIC_ACQUIRE)) {
        publisher_step();
        waitSingle(waiterForUEvent(&g_wake), 50000000);
    }
}

void jsr_saltynx_init(void) {
    if (g_running) return;
    int release_sm = make_session_room();
    if (release_sm) smExit();
    attach(armGetSystemTick());
    if (release_sm) {
        Result rc = smInitialize();
        if (R_FAILED(rc)) debugPrintf("[saltynx] restore SM failed: 0x%x\n", (unsigned)rc);
    }
    __atomic_store_n(&g_stopping, 0, __ATOMIC_RELEASE);
    ueventCreate(&g_wake, true);
    Result rc = threadCreate(&g_publisher, publisher_run, NULL, NULL, 0x8000, 0x3B, -2);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_publisher);
        if (R_SUCCEEDED(rc)) { g_running = 1; return; }
        threadClose(&g_publisher);
    }
    debugPrintf("[saltynx] publisher thread unavailable: 0x%x\n", (unsigned)rc);
    close_mapping();
}

void jsr_saltynx_exit(void) {
    if (g_running) {
        __atomic_store_n(&g_stopping, 1, __ATOMIC_RELEASE);
        ueventSignal(&g_wake);
        threadWaitForExit(&g_publisher);
        threadClose(&g_publisher);
        g_running = 0;
    }
    g_present_seen = 0;
    if (g_block && read_magic(g_block) == NX_FPS_MAGIC) {
        g_block[offsetof(NxFpsBlock, plugin_active)] = 0;
    }
    close_mapping();
    reset_clock();
    g_fps = 0;
    g_average = 0;
    memset(g_present_viewports, 0, sizeof(g_present_viewports));
    g_next_retry = 0;
    g_attempted = 0;
    g_failure_stage = 0;
    g_failure_result = 0;
    g_reported_clear = 0;
    memset(g_viewports, 0, sizeof(g_viewports));
}
