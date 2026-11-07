# NPC timing and SaltyNX candidate

Changes are relative to the supplied `jet-set-radio-nx-main.zip`, not a historical
release tag. No game files are redistributed. See [NPC timing](NPC-TIMING.md) and
[SaltyNX](SALTYNX.md) for the implementation and its limits.

The project routes 54 verified enemy action callbacks through original 30 Hz
updates while retaining the existing player physics/input adapters and 60 Hz
presentation. It renews collision registrations on held presentations and
invalidates cached NPC references when the engine frees their allocations.
This candidate does not supply 60 native skeletal poses per second for these
live NPCs. Original event/tutorial adapters remain separate.

The former graphical FPS counter is removed. Optional telemetry publishes
successful presents and observed viewports using SaltyNX's public shared-memory
protocol. Without SaltyNX the game continues normally. No sysmodule or overlay
is bundled, and no depth-buffer resolution is inferred from a viewport.

Six runtime files missing from the supplied project archive were restored from
the supplied `android32-main.zip`: `util.c/.h`, `watchdog.c/.h` and
`rt_device.c/.h`. Existing runtime sources were preserved.

## Targeted verification

From the project directory:

```sh
python tests/test_npc30.py
python tests/test_npc_collision.py
python -m unittest discover -s tests -v
```

NPC tests require the user's own exact 1.07 ARMv7 `libjetsetradio.so` in the parent
directory, devkitARM, a host C compiler, and Python Unicorn. No game library is
included. The ARM test can load Unicorn from `../test-deps` or the Python
environment. These tests execute controlled fixtures and isolated original
instructions; they do not load a complete stage.

All targeted tests passed. The publisher's production line coverage was 98.05%
with `JSR_TEST_COVERAGE=1`; whole-project 80% coverage is not established.
Independent review found no outstanding verified critical/high finding after
the stale-result validation fix.

The ARM32 payload built successfully with `make -j4 DCR_GL_MESA=1` and
`PORTLIBS` pointing to the supplied Mesa32 prefix in the vita2hos container.
The AArch64 launcher built successfully with `make -j4 A32=/work/runtime` in
`devkitpro/devkita64:latest`. Build `202611042140` passed NSO segment/hash
verification. The final NRO's RomFS was checked to contain the exact newly built
NSP and matching build number. The release ZIP contains only the NRO and empty
folders; the source ZIP excludes game files, linked libraries and build outputs.
Existing runtime/compiler warnings remain; the build does not treat every
warning as an error.

## Console follow-up

Check police pursuit, attacks, graffiti stun and despawns; dogs, projectiles,
vehicles and bosses; tutorial demonstrations and stage transitions. Open Status
Monitor's FPS and resolution displays, then repeat launch with SaltyNX disabled.
These checks remain unproven on hardware. Build success and isolated ARM tests
do not establish that every NPC or scene behaves correctly.
