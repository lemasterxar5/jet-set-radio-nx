/* jsr_obb.c -- the game's data: the expansion file, unpacked once.
 *
 * Jet Set Radio's Android build ships ~598 MB in main.<n>.<pkg>.obb, and that
 * file is itself a plain zip of the Dreamcast-era data: 128 AFS archives under
 * DATA/ (the streets' models, textures, sound banks) and 241 files under
 * CUSTOM/ (the game's own UI: the graffiti canvas, fonts, controls diagrams,
 * help texts). The engine reads them with fopen/opendir/readdir like any
 * other C program, on paths it builds from getBasePath().
 *
 * Android would have mounted the obb read-only and pointed getBasePath() at
 * /storage/emulated/0/Android/obb/<pkg>/. A zip cannot be opened that way, so
 * the first start unpacks it into the app's own files folder -- which the
 * runtime already translates to <game folder>/data/files -- and getBasePath()
 * answers that. The cost is 598 MB on the SD card, once, with the setup's
 * progress bar; after that nothing streams a zip.
 *
 * The expansion file is the only source of the data. There is no "unpack it on
 * a PC and copy the folders" way in: the .obb has to be in the game folder, and
 * an unpacked data/files on the card is never taken as an alternative to it. MIT.
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <switch.h>
#include <switch/runtime/devices/fs_dev.h>
#include <switch/services/fs.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <miniz/miniz.h>

#include "bionic.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "jsr.h"
#include "rt_settings.h"
#include "util.h"

/* The stamp: "obb" + the expansion file's size. A different (or bigger)
 * expansion file unpacks again; the same one never does. */
#define STAMP_KEY "obb-v2"

/* Why the last jsr_data_unpack() gave up, in one line, for the message on the
 * screen: the log has the detail, the player only ever sees this. */
static char g_problem[192];

const char *jsr_data_problem(void) {
  return g_problem[0] ? g_problem : "no reason recorded (see debug.log)";
}

static void set_problem(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_problem, sizeof g_problem, fmt, ap);
  va_end(ap);
  debugPrintf("[data] %s\n", g_problem);
}

/* The card's free space, from the OS; 0 when it cannot be told.
 *
 * NOT b_statfs(): the runtime's is a stub that answers "32 GB, 1 GB free" no
 * matter what (bionic_io.c), which is how an earlier build refused to unpack
 * anything over a gigabyte on a card with 40 GB free. ns is the storage
 * manager, and this is its real answer for the SD card.
 *
 * 0 means unknown, and unknown must never stop the player: the check stands
 * down rather than refuse on a number nobody can vouch for. */
static uint64_t card_free(void) {
  static int ns_ready = -1;
  if (ns_ready < 0) {
    ns_ready = R_SUCCEEDED(nsInitialize()) ? 1 : 0;
    debugPrintf("[data] the storage service: %s\n", ns_ready ? "up" : "NOT available");
  }
  if (!ns_ready)
    return 0;
  s64 free_bytes = 0;
  Result rc = nsGetFreeSpaceSize(NcmStorageId_SdCard, &free_bytes);
  if (R_FAILED(rc) || free_bytes <= 0) {
    debugPrintf("[data] the card's free space could not be read (0x%x)\n", (unsigned)rc);
    return 0;
  }
  return (uint64_t)free_bytes;
}

/* A cheap fingerprint of the expansion file: its size, and its first and last
 * 64 KB. It stands in for a CRC in the .setup stamp, and that is all the stamp
 * needs ("is this the same file?"). rt_crc_of_file() would read all 598 MB
 * just to answer it, which at the start of this step looks like a hang. */
#define FP_CHUNK (64 * 1024)

static char g_base[512]; /* <game folder>/data/files/, with the trailing / */
static int g_base_ready;
static int g_ready;

/* The expansion file in <game folder>/obb: any *.obb, whatever the player or
 * the XAPK called it (main.7.<pkg>.obb for 1.07). Its SD path in `out`.
 * patch is accepted for the shape the other ports' scan has; Jet Set Radio
 * ships one expansion file. */
int jsr_obb_find(int patch, char *out, size_t cap) {
  (void)patch;
  char dir[288];
  rt_root_path(dir, sizeof dir, "obb");
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  int found = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    const char *n = e->d_name;
    size_t len = strlen(n);
    if (len < 5 || len > 200 || n[0] == '.' || strcmp(n + len - 4, ".obb"))
      continue;
    if ((size_t)snprintf(out, cap, "%s/%s", dir, n) >= cap) {
      debugPrintf("[data] the expansion file's name does not fit\n");
      continue;
    }
    found = 1;
    break;
  }
  closedir(d);
  return found;
}

const char *jsr_data_base(void) {
  if (!g_base_ready) {
    /* Android's app files folder, trailing slash: the engine appends
     * "DATA/xxx.afs" to it. The runtime translates it to <root>/data/files. */
    snprintf(g_base, sizeof g_base, "%s/", DCR_ANDROID_FILES);
    g_base_ready = 1;
  }
  return g_base;
}

/* Is the game's data there?
 *
 * NOT jsr_data_base(): that is the ANDROID path the engine is given, and only
 * the engine's own b_* shims run it through dcr_translate_path(). This code
 * calls newlib's stat() directly, so it has to ask about the real SD path --
 * which is where that Android path translates to anyway. */
/* What the engine actually reads, and all of it has to be there. Checking only
 * that DATA/ exists is what let a half-finished unpack pass for a finished one:
 * this used to be one stat, the expansion file ran out of room 861 MB into a
 * 1185 MB job, the last 324 MB of it (one single entry) never landed, and the
 * game started, could not open DATA/PVR/FAT.DAT, and died reading the NULL
 * handle it was given.
 *
 * DATA/PVR/ is the one to watch: it holds the last entry of the expansion file
 * (DATA/PVR/CHANKA.DAT, 324 MB of the 1185), so a tree that ran out of room
 * reaches everything else and stops there. Three stats, at every start. */
int jsr_data_ready(void) {
  return g_ready;
}

static int data_tree_present(void) {
  static const char *const dirs[] = {
      "/data/files/DATA",
      "/data/files/DATA/PVR",
      "/data/files/CUSTOM",
  };
  char probe[DCR_PATH_MAX];
  for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) {
    snprintf(probe, sizeof probe, "%s%s", dcr_game_root(), dirs[i]);
    struct stat st;
    if (stat(probe, &st) != 0 || !S_ISDIR(st.st_mode)) {
      debugPrintf("[data] %s is not there: the game's data is incomplete (see debug.log)\n", probe);
      return 0;
    }
  }
  static int logged;
  if (!logged++) {
    snprintf(probe, sizeof probe, "%s/data/files", dcr_game_root());
    debugPrintf("[data] the game's data is in %s (DATA/, DATA/PVR/ and CUSTOM/ are all there)\n",
                probe);
  }
  return 1;
}

/* The bar's part for the unpack, and the byte total it moves through. */
#define UNPACK_P0 850
#define UNPACK_P1 999

/* What the port's own setup line draws. The runtime's bar is a percentage and
 * nothing else, and a percentage cannot tell a slow card from a hung one: the
 * last entry of the expansion file is 324 MB of the 1185 with the bar pinned at
 * 99%, so "85% and not moving" and "99% and not moving" look exactly the same on
 * screen. The rate and the entry being written are the two things that say
 * which it is. */
static struct {
  uint64_t done, total;
  double mbps;
  const char *file;
} g_prog;

/* ------------------------------------------------------------- extraction
 *
 * The first build measured 1.9 MB/s: 34 ms for every 64 KB that miniz handed
 * over. That is not the card -- it is the NUMBER of writes. Each fwrite to the
 * SD card is its own transaction through the fs service (and each one can cost
 * the card an erase), so 1.19 GB in 64 KB pieces is ~19,000 of them, and the
 * card spends all its time being asked instead of being written.
 *
 * So the bytes are gathered here and flushed in BIG pieces. Same data, ~1/64
 * the transactions. The progress moves once per megabyte instead of once per
 * 64 KB, which is also why the bar and the log are not slowed down by it.
 */
#define WRITE_BUF (8u << 20) /* 8 MB: one transaction per 8 MB written */
#define PROG_STEP (1u << 20) /* the bar moves by this much, not by the chunk */

typedef struct {
  FsFile *f;
  uint8_t *buf;
  size_t n;
  uint64_t written;   /* into this entry */
  uint64_t entry_base; /* the whole job: the bytes before this entry */
  uint64_t total;      /* the whole job's bytes */
  uint64_t last_prog;
  int p0, p1, ok;
} Unpack;

/* Straight to the FS service, one call per WRITE_BUF. newlib's fwrite() was in
 * the way: it is a layer of its own over fsFileWrite, and every layer between
 * the buffer and the card is a chance to split the write up again -- which is
 * the whole thing this extractor exists to avoid. */
static int unpack_flush(Unpack *w) {
  if (!w->n)
    return 1;
  Result rc = fsFileWrite(w->f, (s64)w->written, w->buf, w->n, FsWriteOption_None);
  if (R_FAILED(rc)) {
    debugPrintf("[data] write failed at %llu: 0x%x\n",
                (unsigned long long)w->written, (unsigned)rc);
    w->ok = 0;
    w->n = 0;
    return 0;
  }
  w->written += w->n;
  w->n = 0;
  return 1;
}

/* miniz's writer: returns the whole n it was given, or 0 to abort. */
static size_t unpack_cb(void *opaque, mz_uint64 ofs, const void *src, size_t n) {
  Unpack *w = opaque;
  (void)ofs;
  size_t left = n;
  const uint8_t *s = src;
  while (left) {
    size_t room = WRITE_BUF - w->n;
    size_t take = left < room ? left : room;
    memcpy(w->buf + w->n, s, take);
    w->n += take;
    s += take;
    left -= take;
    if (w->n == WRITE_BUF && !unpack_flush(w))
      return 0;
  }
  /* Once a megabyte, not once per 64 KB: the bar and the log cost nothing
   * measurable this way. */
  if (w->total && w->entry_base + w->written + w->n >= w->last_prog) {
    w->last_prog = w->entry_base + w->written + w->n + PROG_STEP;
    rt_setup_progress_in("Unpacking the game's data", w->p0, w->p1,
                         w->entry_base + w->written + w->n, w->total);
  }
  return n;
}

/* One entry to <dst>.part, then into place, so a half-written file is never
 * mistaken for a whole one (the runtime's extract_to() does the same). */
/* The runtime's bar (log_console_progress) draws a percentage, and a
 * percentage is the one thing that cannot tell a slow card from a stuck one
 * here: the last entry of the expansion file is 324 MB of the 1185, so the bar
 * sits at 99% for a third of the whole job. So this port draws its own line
 * under the bar -- MB done, the rate, and which entry is being written.
 *
 * A frame goes out only when the percentage moves, for the same reason the bar
 * does: one frame per megabyte is 1200 framebuffer submissions, and most of them
 * would show a line identical to the last. */
void port_setup_progress_draw(const char *what, int permille) {
  /* The runtime's bar first: overriding this must not take it away. */
  log_console_progress(what, permille);
  if (!g_prog.total)
    return;
  char line[104];
  snprintf(line, sizeof line, "%3d%%  %llu / %llu MB  %.1f MB/s  %s", permille / 10,
           (unsigned long long)(g_prog.done / 1048576ull),
           (unsigned long long)(g_prog.total / 1048576ull), g_prog.mbps,
           g_prog.file ? g_prog.file : "");
  /* The rate is the part worth watching, so the line is redrawn when it says
   * something new rather than when the percentage moves -- and a frame is only
   * pushed with it. A frame per megabyte (the unpack's own cadence) is 1200
   * framebuffer submissions for a line that barely changes. */
  static char last[sizeof line];
  if (!strcmp(last, line))
    return;
  snprintf(last, sizeof last, "%s", line);
  /* Row 26: the bar above lives on 18, 20, 23 and 25. */
  const int cols = 80;
  int pad = (cols - (int)strlen(line)) / 2 + 1;
  if (pad < 1)
    pad = 1;
  printf("\x1b[26;%dH%s", pad, line);
  fflush(stdout);
  consoleUpdate(NULL);
}

static int extract_big(mz_zip_archive *zip, int idx, const char *dst, uint8_t *buf, uint64_t entry_base,
                       uint64_t total, int p0, int p1) {
  char tmp[DCR_PATH_MAX + 8];
  snprintf(tmp, sizeof tmp, "%s.part", dst);
  rt_mkdirs_for(dst);
  FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
  if (!fs)
    return -1;
  /* Paths for these go WITHOUT the "sdmc:" prefix: the FsFileSystem is the
   * device's own, so the path is relative to its root. Passing the full path
   * (which is what rt_root_path gives, and what fopen takes) is not a path
   * the service recognises, and every one of the 625 entries failed here --
   * the folders got made, nothing else did. */
  const char *dev = strstr(tmp, "sdmc:");
  const char *rel = dev ? dev + 5 : tmp;
  const char *drel = dst + (rel - tmp); /* tmp is dst with ".part" on the end */
  fsFsDeleteFile(fs, rel); /* a .part left by an entry that did not finish */
  FsFile f;
  mz_zip_archive_file_stat st;
  if (!mz_zip_reader_file_stat(zip, (mz_uint)idx, &st))
    return -1;
  Result rc = fsFsCreateFile(fs, rel, (s64)st.m_uncomp_size, 0);
  if (R_SUCCEEDED(rc))
    rc = fsFsOpenFile(fs, rel, FsOpenMode_Write, &f);
  if (R_FAILED(rc)) {
    debugPrintf("[data] create/open %s failed: 0x%x\n", rel, (unsigned)rc);
    fsFsDeleteFile(fs, rel);
    return -1;
  }
  Unpack w = {&f, buf, 0, 0, entry_base, total, entry_base, p0, p1, 1};
  int ok = mz_zip_reader_extract_to_callback(zip, (mz_uint)idx, unpack_cb, &w, 0) && w.ok;
  if (!ok)
    debugPrintf("[data] extract %s failed: %s\n", rel,
                mz_zip_get_error_string(mz_zip_get_last_error(zip)));
  if (ok && !unpack_flush(&w))
    ok = 0;
  if (w.written != st.m_uncomp_size)
    ok = 0;
  if (ok) {
    rc = fsFileFlush(&f);
    if (R_FAILED(rc)) {
      debugPrintf("[data] flush %s failed: 0x%x\n", rel, (unsigned)rc);
      ok = 0;
    }
  }
  fsFileClose(&f);
  if (!ok) {
    fsFsDeleteFile(fs, rel);
    return -1;
  }
  fsFsDeleteFile(fs, drel);
  rc = fsFsRenameFile(fs, rel, drel);
  if (R_FAILED(rc)) {
    debugPrintf("[data] rename %s failed: 0x%x\n", rel, (unsigned)rc);
    fsFsDeleteFile(fs, rel);
    return -1;
  }
  return 0;
}

/* ------------------------------------------------------------ the unpack */

static int unpack_into_files(const char *obb, uint64_t total) {
  char files[256];
  rt_root_path(files, sizeof files, "data/files");
  rt_mkdirs(files);

  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, obb, 0)) {
    set_problem("the expansion file is not a readable zip (%s)",
                mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    return -1;
  }
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  debugPrintf("[data] %s: %u entries, %llu MB", obb, (unsigned)n,
              (unsigned long long)(total / 1048576ull));

  /* The bar counts the bytes written over the whole expansion file, so it
   * moves once, from UNPACK_P0 to UNPACK_P1, across all the entries.
   *
   * rt_setup_extract_entry()'s p0..p1 are a global permille range and it
   * sweeps that range with the bytes of the ONE entry it is given. Handed the
   * whole 850..999 every time, a 638-entry zip is 638 sweeps of the full bar
   * and it jumps back to 85% after each file; handed NULL it draws nothing
   * and the bar only steps once a whole entry is done -- which looks stuck,
   * because the first 241 entries (CUSTOM/) are 2% of the data.
   *
   * So each entry is given the SLICE of the bar that its own bytes own, and
   * the slices tile the range in order: the bar moves with every write and
   * never goes backwards. */
  /* One buffer for the whole job, allocated once: the card is written in
   * 2 MB pieces, and that is the only thing that made it fast. */
  uint8_t *wbuf = malloc(WRITE_BUF);
  if (!wbuf) {
    mz_zip_reader_end(&zip);
    set_problem("no memory for the unpack's write buffer (%u KB)", (unsigned)(WRITE_BUF / 1024));
    return -1;
  }

  uint64_t done = 0, next_log = 0, t0 = armGetSystemTick();
  unsigned failed = 0, entries = 0;
  for (mz_uint i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st))
      continue;
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;
    char dst[DCR_PATH_MAX];
    /* A zip entry that would climb out of the folder, or that is too long to
     * name, is not written. */
    if (strstr(st.m_filename, "..") || (size_t)snprintf(dst, sizeof dst, "%s/%s", files, st.m_filename) >= sizeof dst) {
      debugPrintf("[data] skipped %s\n", st.m_filename);
      continue;
    }
    entries++;
    /* This entry's own slice of the bar: the permille range its bytes own.
     * The bar moves with every write inside the slice, and the slices tile
     * the range in order, so it never goes back. */
    int p0 = UNPACK_P0, p1 = UNPACK_P0;
    if (total) {
      p0 = UNPACK_P0 + (int)((uint64_t)(UNPACK_P1 - UNPACK_P0) * done / total);
      p1 = UNPACK_P0 + (int)((uint64_t)(UNPACK_P1 - UNPACK_P0) * (done + st.m_uncomp_size) / total);
      if (p1 <= p0)
        p1 = p0 + 1; /* a sliver still has to draw something */
    }
    /* An entry that cannot be written is counted, not merely mentioned. This
     * used to be a log line and the job carried on to the end and returned 0,
     * so the caller wrote the "done" stamp over a half-unpacked tree: the game
     * then started, could not find the files that were missing, and died
     * reading a handle that came back NULL. The last 324 MB of the expansion
     * file is one single entry (DATA/PVR/CHANKA.DAT), which is what went. */
    if (extract_big(&zip, (int)i, dst, wbuf, done, total, p0, p1)) {
      if (failed < 12)
        debugPrintf("[data] %s (%llu MB) could not be extracted\n", st.m_filename,
                    (unsigned long long)(st.m_uncomp_size / 1048576ull));
      failed++;
    }
    done += st.m_uncomp_size;
    /* Proof of life in the log: every 8 MB, with the rate, the entry being
     * written and what is left. Without it the log is silent for the whole
     * unpack, which is indistinguishable from a hang.
     *
     * Every 8 MB rather than every 64 because the bar on screen only moves in
     * whole percentages, and the last entry is 324 MB of the 1185 with the bar
     * pinned at 99%: with 64 MB steps that is five lines over half a minute of
     * writing one file, which reads as "it stopped at 85%". */
    if (done >= next_log) {
      next_log = done + (8ull << 20);
      u64 ns = armTicksToNs(armGetSystemTick() - t0);
      double rate = ns ? (double)done / (double)ns * 1e9 / 1048576.0 : 0.0;
      double left = rate > 0.0 ? (double)(total - done) / (rate * 1048576.0) : 0.0;
      debugPrintf("[data]   %llu / %llu MB (%.0f%%) at %.1f MB/s, about %.0f s left -- %s\n",
                  (unsigned long long)(done / 1048576ull), (unsigned long long)(total / 1048576ull),
                  100.0 * (double)done / (double)(total ? total : 1), rate, left, st.m_filename);
      /* The same numbers go to the port's own line on screen: the bar alone
       * cannot say whether the card is slow or the whole thing is stuck. */
      g_prog.done = done;
      g_prog.total = total;
      g_prog.mbps = rate;
      g_prog.file = st.m_filename;
    }
  }
  if (total)
    rt_setup_progress_in("Unpacking the game's data", UNPACK_P0, UNPACK_P1, total, total);
  free(wbuf);
  mz_zip_reader_end(&zip);
  g_prog.total = 0;
  g_prog.file = NULL;
  if (failed) {
    debugPrintf("[data] %u of %u entries (%llu MB) could not be extracted; the stamp is not "
                "written, so this runs again next time\n",
                failed, entries, (unsigned long long)(done / 1048576ull));
    return -1;
  }
  debugPrintf("[data] %llu bytes in %s\n", (unsigned long long)done, files);
  return 0;
}

/* A cheap fingerprint of the expansion file: its size, and its first and last
 * 64 KB. It stands in for a CRC in the .setup stamp, and that is all the stamp
 * needs ("is this the same file?"). rt_crc_of_file() would read all 598 MB
 * just to answer it, which at the start of this step looks like a hang. */
static unsigned long obb_fingerprint(const char *path, long size) {
  unsigned long h = 1469598103u ^ (unsigned long)size;
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  unsigned char *buf = malloc(FP_CHUNK);
  if (!buf) {
    fclose(f);
    return 0;
  }
  long offs[2] = {0, size > FP_CHUNK ? size - FP_CHUNK : 0};
  for (int k = 0; k < 2; k++) {
    size_t want = (offs[k] == 0 && size <= FP_CHUNK) ? (size_t)size : FP_CHUNK;
    if (!want || fseek(f, offs[k], SEEK_SET))
      continue;
    size_t got = fread(buf, 1, want, f);
    for (size_t i = 0; i < got; i++) {
      h ^= buf[i];
      h *= 16777619u;
    }
  }
  free(buf);
  fclose(f);
  return h;
}

/* The expansion file is the data, and this is the only way in: it is in the
 * game folder or the game will not start. An unpacked data/files on the card
 * was once accepted as an alternative (it is what a player who unpacked the
 * .obb by hand has), and it only ever made a broken install -- a folder
 * copied out of a zip is not something this port can check, so the two ways
 * disagreed about what the game had and the engine died reading a file that
 * was not there.
 *
 * So: no .obb, no game. The stamp below is the one shortcut that stays, and it
 * is not this one -- it says "this exact expansion file was unpacked to
 * completion on the console", which is the port's own work. */
int jsr_data_unpack(RtSetupCtx *ctx) {
  (void)ctx;
  g_ready = 0;
  char obb[DCR_PATH_MAX];
  if (!jsr_obb_find(0, obb, sizeof obb)) {
    set_problem("no expansion file in sdmc:" PORT_ROOT_PATH "/obb\n\n"
                "Copy main.<n>.<pkg>.obb out of your XAPK into\n"
                "  " PORT_ROOT_PATH "/obb/\n"
                "and start again. The console unpacks it itself, the first time\n"
                "it runs -- there is nothing to unpack anywhere else.");
    return -1;
  }
  long size = rt_file_size(obb);
  unsigned long crc = obb_fingerprint(obb, size);
  unsigned long stamp_crc = 0, stamp_size = 0;
  if (rt_setup_stamp_get(STAMP_KEY, &stamp_crc, &stamp_size) && stamp_crc == crc &&
      stamp_size == (unsigned long)size && data_tree_present()) {
    g_ready = 1;
    return 0; /* the same expansion file, unpacked here to the last entry */
  }

  /* The total, for the bar: the zip's own central directory. */
  mz_zip_archive zip;
  uint64_t total = 0;
  memset(&zip, 0, sizeof zip);
  if (mz_zip_reader_init_file(&zip, obb, 0)) {
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; i++) {
      mz_zip_archive_file_stat st;
      if (mz_zip_reader_file_stat(&zip, i, &st) && !mz_zip_reader_is_file_a_directory(&zip, i))
        total += st.m_uncomp_size;
    }
    mz_zip_reader_end(&zip);
  }
  /* The card has to hold the whole thing, and the free space is asked for
   * now rather than discovered at the 90% mark. The 8% is headroom for the
   * .part files the extractor writes beside each entry. */
  uint64_t free_bytes = card_free();
  uint64_t need = total + total / 12;
  /* If the card would not say how much room it has, say that here rather than
   * walking past the check: this used to print nothing at all and unpack
   * anyway, and ran the card out of room 861 MB into a 1185 MB job. */
  if (!free_bytes)
    debugPrintf("[data] the card did not report its free space; unpacking %llu MB without "
                "knowing there is room, and stopping if a write fails\n",
                (unsigned long long)(need / 1048576ull));
  if (free_bytes && free_bytes < need) {
    set_problem("not enough room on the card: %llu MB free, %llu MB needed\n"
                "(%llu MB of game data, plus the .obb itself). Free up about\n"
                "%llu MB and start again -- there is no other way to put the\n"
                "data on the card than this.",
                (unsigned long long)(free_bytes / 1048576ull), (unsigned long long)(need / 1048576ull),
                (unsigned long long)(total / 1048576ull),
                (unsigned long long)((need - free_bytes + 1048575ull) / 1048576ull));
    return -1;
  }
  if (free_bytes)
    debugPrintf("[data] the card has %llu MB free, the data needs %llu MB\n",
                (unsigned long long)(free_bytes / 1048576ull), (unsigned long long)(need / 1048576ull));
  if (unpack_into_files(obb, total)) {
    set_problem("The game's data could not be unpacked.\n\n"
                "debug.log names the files and extraction errors.\n"
                "Check the OBB and SD card, then start again.\n"
                "The game cannot start with incomplete data.");
    return -1;
  }
  rt_setup_stamp_set(STAMP_KEY, crc, (unsigned long)size);
  rt_setup_stamp_save();
  g_ready = data_tree_present();
  return g_ready ? 1 : -1;
}

/* ---------------------------------------------------------------- paths */

/* Both of these are the pattern the other ports that stream their data use
 * (TDKR, tasm2), and TDKR says why it needs them: "the runtime's default only
 * traces the APK; without this the zone streaming crash leaves no [io]/[path]
 * lines at all". With the game's data on a card and the engine reading through
 * fopen(), a path that does not resolve is otherwise invisible.
 *
 * Trace the game's own data (the runtime's own limit of 64 lines is spent on
 * the APK otherwise) and nothing else. */
int dcr_path_traced(const char *p) {
  if (!p)
    return 0;
  return strstr(p, "/DATA/") || strstr(p, "/CUSTOM/") || strstr(p, "/userdata/") ||
         strstr(p, ".afs") || strstr(p, ".pvr") || strstr(p, ".fnt");
}

/* The runtime sends /sdcard/Android/obb/<pkg>/... to <root>/obb/... and
 * /data/data/<pkg>/... to <root>/data/... (dcr_path.c). The engine builds its
 * names off getBasePath(), so those two are normally all it needs -- but a
 * port must be able to answer the rest, and the ones that stream data have a
 * second reason: the extracted tree lives in data/files while the engine can
 * still ask under obb/ (its own resource dir). So:
 *   obb/<...>            -> data/files/<...>  when that is what exists
 *   <...>.obb            -> the .obb of that name in obb/, when the one asked
 *                           for is not there under that name
 * A path that exists as asked for is never replaced. */
const char *port_path_fixup(const char *real, char *out, size_t cap) {
  if (!real)
    return real;
  char fixed[DCR_PATH_MAX];
  int is_fixed = 0;

  /* obb/<...> -> data/files/<...> (the extracted data). */
  char obb[DCR_PATH_MAX];
  snprintf(obb, sizeof obb, "%s/obb/", dcr_game_root());
  size_t n = strlen(obb);
  if (!strncmp(real, obb, n)) {
    struct stat st;
    if (stat(real, &st) != 0) {
      char alt[DCR_PATH_MAX];
      snprintf(alt, sizeof alt, "%s/data/files/%s", dcr_game_root(), real + n);
      if (stat(alt, &st) == 0) {
        static int logged;
        if (!logged++)
          debugPrintf("[data] %s -> %s (the unpacked data)\n", real, alt);
        snprintf(fixed, sizeof fixed, "%s", alt);
        is_fixed = 1;
      }
    }
  }

  /* An .obb asked for by a name we do not have -> the one that is there. */
  if (!is_fixed) {
    size_t l = strlen(real);
    if (l >= 5 && !strcasecmp(real + l - 4, ".obb")) {
      struct stat st;
      if (stat(real, &st) != 0) {
        char found[DCR_PATH_MAX];
        if (jsr_obb_find(0, found, sizeof found)) {
          static int logged;
          if (!logged++)
            debugPrintf("[data] %s -> %s (the expansion file we have)\n", real, found);
          snprintf(fixed, sizeof fixed, "%s", found);
          is_fixed = 1;
        }
      }
    }
  }

  if (!is_fixed)
    return real;
  snprintf(out, cap, "%s", fixed);
  return out;
}

/* The RtSetupStep's run(): the data, after the library and the class list. */
void jsr_data_stage(RtSetupCtx *ctx) { jsr_data_unpack(ctx); }
