#---------------------------------------------------------------------------------
# Jet Set Radio -- Nintendo Switch wrapper (32-bit / AArch32), on the android32
# runtime (runtime/). Ships no game code and no game data: the player's own APK
# and OBB are read at run time. Build with ./build.sh.
#
# Output: jsr_nx.nsp (the 32-bit program: ExeFS main + main.npdm) and
# jsr_nx.build; ./launcher/build.sh then makes jsr_nx.nro around them.
#---------------------------------------------------------------------------------
TARGET               := jsr_nx
PORT_NPDM_PROGRAM_ID := 0x0100000000001040
PORT_CFLAGS := -DJSR_NATIVE60_DEFAULT=1

include runtime/runtime.mk

# -O3 over the runtime's -O2: this wrapper is CPU-bound (the engine is
# translated on the fly), and --gc-sections drops the dead code dcr32.ld's
# KEEPs already allow. LTO stays one uncomment away; it needs a clean Docker
# rebuild to verify.
CFLAGS  += -O3
LDFLAGS += -Wl,--gc-sections
#CFLAGS  += -flto -fno-fat-lto-objects -flto-partition=balanced -fipa-pta
#LDFLAGS += -flto -fno-fat-lto-objects -flto-partition=balanced -fipa-pta
