# Jet Set Radio — Nintendo Switch (JSR NX)

An unofficial homebrew wrapper for the Android ARMv7 version of Jet Set Radio on Nintendo Switch, powered by **android32**. It runs the game's ARM code natively: this is not an Android emulator or a rewrite of the game.

**Release: 1.0.0.** No APK, OBB, game libraries or commercial game assets are included. You must supply the files from your own legitimate copy. This project is not affiliated with SEGA or Nintendo.

## Performance and 60 FPS

According to the author's console testing, the game runs at **60 FPS at stock clocks**, and can even maintain 60 FPS in tested scenes at **607 MHz CPU / 151 MHz GPU**. These are author-reported measurements, not a guarantee for every area, SD card or configuration. Loading and demanding scenes may cause dips.

The `native60` mode is enabled by default. It adapts engine timing and evaluates character poses at the presentation rate, without matrix interpolation or generated intermediate frames. Recorded commands and some tutorial clocks remain at 30 Hz to preserve synchronization. This does not mean that every original engine subsystem has been converted to 60 Hz. Default object draw distance is **×2.5**, with detail distance at **×1.5**.

**Clock measurement note:** by default, the port requests a temporary CPU boost while loading and a handheld GPU boost to 460.8 MHz. To test strictly at stock clocks or 607/151 without these requests, disable both in `/switch/jsr_nx/config.ini`:

```ini
[performance]
boost_cpu_when_loading=false
gpu_boost_handheld=false
```

The port does not set 607/151 MHz automatically. Those clocks must be configured externally.

## FPS counter — disabled by default

The on-screen FPS counter is still included, but **is disabled by default**. To enable it manually:

1. Launch the game once to generate `/switch/jsr_nx/config.ini`, then close it completely.
2. Open that file and change `fps_overlay` in the existing `[display]` section:

   ```ini
   [display]
   fps_overlay=true
   ```

3. Save the file and restart the game. The counter appears in the top-left corner and updates twice per second.

Set `fps_overlay=false` and restart to hide it again. If `[display]` already exists, edit its existing setting rather than adding a duplicate section or key. There is no controller shortcut or in-game toggle for this option.

**Upgrading from an earlier build:** existing settings are preserved. If your current file already contains `fps_overlay=true`, the counter stays enabled until you change it to `false`. The new default applies to fresh configurations and missing settings; it does not overwrite an explicit user choice. You do not need to delete your config or saves.

The counter measures presented frames, not the frequency of every simulation subsystem. `show_fps` is a separate debug-log option, not the on-screen overlay, and is also disabled by default. Hiding the overlay does not disable native 60 FPS gameplay.

## Requirements and installation

- A homebrew-capable Nintendo Switch with support for running AArch32 programs.
- **Jet Set Radio for Android 1.07 / 1.0.7**, versionCode `7`, package `com.sega.jetsetradio`. The hooks depend on this version; other versions are not supported.
- Your own APK and expansion file, `main.7.com.sega.jetsetradio.obb`.
- Approximately 1.9 GB of free space for the APK, OBB and extracted data; allow additional room for saves and temporary files.

Copy the release's `switch` folder to the SD card root, then add your own game files:

```text
switch/
└── jsr_nx/
    ├── jsr_nx.nro
    ├── jsr.apk                              ← your APK, not included
    └── obb/
        └── main.7.com.sega.jetsetradio.obb   ← your OBB, not included
```

The distributed release contains only the NRO and folders, with an empty `obb/`. The first launch extracts your game resources and may take some time; do not interrupt the writes.

This launcher needs a **dedicated forwarder created for `/switch/jsr_nx/jsr_nx.nro`**, for example through sphaira: `Homebrew > Jet Set Radio > Install Forwarder`. Launch its own HOME-menu icon and follow the launcher instructions to install/update the 32-bit payload. Album applet mode is not supported, and generic hbmenu title takeover does not replace the matching forwarder. The NRO contains the homebrew payload, not the commercial game engine.

## Controls

Default native MOGA-style layout:

| Switch input | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Camera |
| D-pad | Menu navigation |
| A | Jump / Confirm |
| B | Back / Cancel |
| L / ZL | Graffiti / Spray |
| R / ZR | Dash |
| + | Pause |
| − | Android / system Back |
| Touchscreen | Touch controls / menus |

X and Y have no dedicated action in this mode. MOGA input is enabled by default with `[controls] moga=true`. The legacy touch-mapping mode is an alternative and uses a different layout.

## Building from source

This folder includes the wrapper and runtime sources, but **no external libraries, SDKs, game files or build outputs**.

External requirements:

1. Docker and a POSIX shell (Linux/WSL). The AArch32 build uses `ghcr.io/vita2hos/devcontainer/vita2hos:latest`, providing devkitARM, switch-tools and AArch32 miniz.
2. [aks796's libnx32](https://github.com/aks796/libnx32), built with its headers and `libnx.a` / `libnxd.a`. Point `DCR_LIBNX32` at its `prefix/` directory.
3. [aks796's Mesa32](https://github.com/aks796/mesa32): place its `include/` and `lib/` directories inside `portlibs32/`, next to the root Makefile. Include the corresponding static dependencies.
4. For the AArch64 launcher, devkitA64/libnx through `devkitpro/devkita64:latest`.

```sh
chmod +x build.sh launcher/build.sh runtime/tools/*.sh runtime/launcher/build.sh
DCR_LIBNX32=/absolute/path/libnx32/prefix sh build.sh
sh launcher/build.sh
```

Output: `launcher/jsr_nx.nro`. Building the wrapper does not require an APK. `tools/elf32_nso.py` uses LZ4 if installed in the build environment's Python; otherwise it produces an uncompressed NSO. The LZ4 package is not bundled. The root build script rejects a missing Mesa installation to avoid accidentally building a null renderer.

Toolchain images are not pinned to a digest. The clean source export has not been rebuilt end-to-end with Docker; packaging checks do not replace that test. The release payload was rebuilt using the existing local devkit toolchain and dependencies.

## Status and limitations

- Release 1.0.0 includes the rebuilt payload with the FPS overlay disabled by default. The public release version and internal timestamp build number are separate identifiers.
- It retains the empty shadow-batch fix for the crash observed in the second tutorial. Targeted tests cover the fix, but a complete console tutorial retest is still needed.
- Native 60 FPS includes experimental engine adaptations; flawless behavior across every tutorial, transition and physics interaction is not guaranteed.
- Android's `playVideo` call is not implemented; cinematic playback is not guaranteed.
- For emulators with broken ETC1 handling, `[display] decode_etc1=true` is available. On Switch, keep its default `false` to retain native compression.
- When reporting an issue, include the build number and relevant logs only. Never attach APKs, OBBs, game libraries or private saves.

Technical timing notes and development history (in Spanish): [docs/NATIVE60.md](docs/NATIVE60.md).

## Acknowledgements

Thank you to everyone developing and maintaining the libraries and tools that make this work possible:

- **aks796**, for [android32](https://github.com/aks796/android32), [libnx32](https://github.com/aks796/libnx32) and [Mesa32](https://github.com/aks796/mesa32).
- **xerpi and the vita2hos team**, for the foundations of the [AArch32 Switch environment](https://github.com/vita2hos).
- **Switchbrew and the libnx authors**, for [libnx](https://github.com/switchbrew/libnx) and platform documentation.
- **devkitPro and its contributors**, for the tools, toolchains, and Switch ports of [Mesa](https://github.com/devkitPro/mesa) and [libdrm_nouveau](https://github.com/devkitPro/libdrm_nouveau).
- **The Mesa developers**, and **Red Hat and the other libdrm authors**, for the original graphics components.
- **Andy Nguyen and fgsfds**, for the shared-library loading and adaptation work from which `so_util` derives.
- **Rich Geldreich, Tenacious Software, RAD Game Tools, Valve and [miniz](https://github.com/richgel999/miniz) contributors**, for archive reading and decompression.
- **Jean-loup Gailly, Mark Adler and [zlib](https://zlib.net/) contributors**, and the **[Expat](https://github.com/libexpat/libexpat)** and **[BLAKE3](https://github.com/BLAKE3-team/BLAKE3)** teams, for dependencies used by Mesa variants supported in the environment.
- **The GCC, newlib, binutils, Python and Docker teams**, and **the LZ4 authors**, for the build and packaging tools.
- **The FFmpeg authors**, for their work and dependencies supplied during development. FFmpeg is not linked into this release and does not imply current video support.
- **SEGA and the original creators of Jet Set Radio**, for the game. Their commercial code and assets are not redistributed here.

### Special thanks

A very special thank you to **[Seamus McGrath (@seamusduncmcgrath)](https://github.com/seamusduncmcgrath)**, creator of the **[Call of Duty: Black Ops Zombies Nintendo Switch port](https://github.com/seamusduncmcgrath/cod-boz-arm64-nx)**. His work provided a key technical reference and **made the implementation of real 60 FPS in this port possible**. Thank you for sharing it with the community. Credit also goes to **Producdevity** and the contributors to the [project that port derives from](https://github.com/Producdevity/cod-boz-port).

Jet Set Radio's adaptation is specific to its own engine. These thanks do not imply that both games share an engine or that Seamus is responsible for this wrapper's changes.

## Licensing

Original source licenses and notices are preserved. See [LICENSING.md](LICENSING.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Acknowledgements do not replace each dependency's license conditions. The game, trademarks and cover artwork belong to their respective rights holders.
