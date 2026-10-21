/* port_config.h -- Jet Set Radio (SEGA/Revolution, com.sega.jetsetradio 1.07,
 * armeabi-v7a): the port's settings for the android32 runtime.
 *
 * Macros only (the assembler and the launcher read this too).
 *
 * Folder on the SD card (PORT_ROOT_PATH = "/switch/" PORT_NAME):
 *   sdmc:/switch/jsr_nx/
 *     jsr_nx.nro        the launcher
 *     <any name>.apk    the player's APK (found by its manifest and the game's library)
 *     obb/              the expansion file, unpacked once into data/files/
 *     data/files/DATA/  the game's data, once unpacked
 *     data/files/CUSTOM/
 *     config.ini, debug.log, libjetsetradio.so (unpacked), classes.txt
 * MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define PORT_TITLE   "Jet Set Radio"
#define PORT_NAME    "jsr_nx"
#define PORT_PACKAGE "com.sega.jetsetradio"
#define PORT_BANNER  "jsr_nx: Jet Set Radio (KATANA/Ninja engine, armeabi-v7a)"

#define PORT_APK_DEFAULT_NAME "jsr.apk"
#define PORT_APK_DESC "Jet Set Radio 1.07 (" PORT_PACKAGE ", armeabi-v7a)"
/* The APK, under any name: it must hold the game's library and be this package. */
#define PORT_APK_ROLES \
  {.what = "the game", .name = PORT_APK_DEFAULT_NAME, \
   .need = (const char *const[]){"lib/armeabi-v7a/libjetsetradio.so", NULL}, .package = PORT_PACKAGE}

#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's library and its 598 MB data)"

/* libjetsetradio.so: highest p_vaddr + p_memsz = 0x67916a (6.5 MB).
 * libjetsetradio-nn.so, the alternative build the wrapper can load, asks for
 * 0x6cd3fa (7.1 MB), so one region covers both. */
#define PORT_SO_REGION_BYTES (16u * 1024 * 1024)

/* The data is Dreamcast-era AFS archives read through stdio in many small
 * pieces: coalesce them, and buffer 32 KB instead of 1 KB. */
#define RT_IO_READAHEAD   1
#define RT_IO_PATTERN_STATS 1
#define RT_STDIO_READ_BUF 32768

/* One player: the game has no controller support of its own. */
#define RT_PAD_MAX_PLAYERS 1

/* step() runs the whole frame (loopStep), so the boost watcher cannot poll
 * between frames: it watches on a thread of its own and boosts a frame past
 * 50 ms (the load screens stream hundreds of MB off the SD card). */
#define RT_BOOST_WATCH_THREAD 1

#endif /* PORT_CONFIG_H */