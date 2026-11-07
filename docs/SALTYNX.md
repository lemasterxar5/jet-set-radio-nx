# SaltyNX measurements

The port publishes successful EGL presents and observed OpenGL viewports to
SaltyNX shared memory. Install and enable [SaltyNX](https://github.com/masagrator/SaltyNX)
and [Status Monitor Overlay](https://github.com/masagrator/Status-Monitor-Overlay),
then open the overlay's FPS or resolution display. The port does not draw an
internal FPS counter. Without the sysmodule, the game continues normally and
retries the optional connection once per second.

FPS is measured from actual successful `eglSwapBuffers` calls against the Switch
19.2 MHz monotonic timer. It is independent of the engine's target frame rate.
The shared integer value covers a one-second window; the average covers the
last ten frame intervals. This measures application presents, not a hardware
scanout trace or the rate of simulation updates.

The resolution display reports up to eight observed `glViewport` dimensions,
ordered by their call count in the most recently presented frame. It uses the
overlay's landscape size filters. If the game did not issue an eligible viewport
call in that frame, the EGL surface size is the fallback. This is viewport
telemetry: it does not establish the resolution of every intermediate render
target. Depth-buffer dimensions remain empty because this publisher does not
hook depth attachments. The overlay's depth-request sentinel is acknowledged.

This integration only publishes measurements. FPSLocker frame caps, patches,
buffer controls, refresh-rate changes and ReverseNX modes are not implemented
by this publisher. No SaltyNX plugin is bundled or injected into the game.
The direct shared-memory route also works with the port's AArch32 payload,
without depending on the automatic injection policy for homebrew forwarders.

## Implementation contract

`source/jsr_saltynx.h` exposes startup/shutdown and render capture functions:

- `jsr_saltynx_init()` connects before guest constructors and EGL initialization.
- `jsr_saltynx_record_viewport(width, height)` records each game viewport call.
- `jsr_saltynx_present(surface_width, surface_height)` captures timing and viewport
  samples after a successful swap; it never connects, maps or writes shared memory.
- `jsr_saltynx_exit()` signals and joins the publisher before releasing its mapping.

A native libnx thread publishes snapshots every 50 ms and retries attachment once
per second. A short Mutex protects 64-bit sample data on ARM32; IPC runs outside
that lock. Without a new frame for one second, reported FPS becomes zero without
inventing additional presents. Thread creation failure is nonfatal and logged.

Startup checks the process session resource budget. If exhausted, it temporarily
releases the SM connection during the early synchronous attachment, then restores
SM before continuing startup. Background retries never close SM. Kernel resource
limits are not raised. The existing shared-memory permissions and syscall set
remain sufficient.

The early attachment, session-budget check and independent publisher follow the
approach reviewed in [NaGaa95's NetherSX2 integration](https://github.com/NaGaa95/NetherSX2_nx/blob/b21dcb14ab65fa7c82373344161bce32709effb1/source/saltynx.cpp).
ARM32 byte writes and actual viewport telemetry are retained. `InjectServ` is not
used as an IPC fallback: the official server accepts and closes that probe port
without implementing commands 6 or 7.

The publisher uses `SaltySD` commands 7 (shared-memory handle), 6 (reserve a
block if absent), and 0 (end session). Sessions close after attachment; the worker
writes directly to the mapped page. Failed attachment releases acquired handles
and mapping resources. Existing blocks are reused because the reservation API
has no release command. Offset zero is valid; alignment and page bounds are
checked before writing. Connection, IPC, mapping or allocation failures are
intentionally nonfatal and receive the same bounded retry policy.

The implementation targets the current packed 174-byte `NxFpsSharedBlock` ABI.
Compile-time checks pin important field offsets. Packed values are published
with byte stores, avoiding unaligned AArch32 accesses. Overlay control fields
are preserved in an existing block. Every successful present refreshes the
activity heartbeat, which the overlay itself consumes. The public protocol is
implemented directly; no SaltyNX library source is copied into this port.

Protocol references checked on 2026-11-04:

- [SaltyNX service implementation](https://github.com/masagrator/SaltyNX/blob/master/saltysd_proc/source/service.c)
- [Status Monitor shared-block layout](https://github.com/masagrator/Status-Monitor-Overlay/blob/master/source/Utils.hpp)
- [Status Monitor resolution consumer](https://github.com/masagrator/Status-Monitor-Overlay/blob/master/source/modes/Resolutions.hpp)
- [SaltyNX automatic injection policy](https://github.com/masagrator/SaltyNX/blob/master/saltysd_proc/source/hijack.c)

## Verification

Run `python tests/test_saltynx.py` with Clang or a host C compiler available.
On Windows, `JSR_TEST_COVERAGE=1` additionally uses the compiler's adjacent
`llvm-profdata.exe` and `llvm-cov.exe` tools and requires at least 80% line
coverage of the production publisher. The deterministic Horizon fixture covers
the packed ABI, real 60/30 FPS timing, clock discontinuities, heartbeat renewal,
viewport collection, sentinel acknowledgment, reservation validation, service
absence, failed IPC/mapping, recovery after SaltyNX clears the API field while
preserving offset-zero magic, thread lifecycle, IPC-free render capture, session
exhaustion recovery and resource cleanup. The latest run reached 98.91%
line coverage. The C file also passes a devkitARM syntax check against the
supplied libnx32 headers with warnings treated as errors.

An on-console check remains necessary: open the overlay during gameplay and
cinematics, confirm nonzero FPS and expected viewport dimensions, then launch
again with SaltyNX disabled to verify the optional fallback. Host mocks cannot
prove sysmodule installation, firmware compatibility or the displayed result on
real hardware. Future incompatible shared-memory ABI changes require a protocol
update.

The diagnostic candidate writes `[saltynx]` lines to the port's `debug.log`.
Connection, handle acquisition, mapping and reservation failures include their
Result code. Identical repeated failures are suppressed. Successful attachment,
the first successful EGL present and the first published heartbeat are logged.
The API identifier is restored on every present, including after `cleanAppData`
clears bytes after the first magic. This recovery is tested; missing initial FPS
still requires a console log to identify the failing boundary.
