/* jsr_launcher.c -- the launcher's checks for the game's data: besides the
 * APK, Jet Set Radio needs its ~598 MB expansion file in <game folder>/obb,
 * which the first start unpacks into data/files. MIT. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "launcher.h"
#include "rt_settings.h"
#include "jsr_obb_scan.h"

#define OBB_DIR "sdmc:" PORT_ROOT_PATH "/obb"
#define FILES_DIR "sdmc:" PORT_ROOT_PATH "/data/files"

int port_launcher_check(char *status, size_t scap, char *help, size_t hcap) {
  char path[512];
  if (jsr_obb_scan(OBB_DIR, 0, path, sizeof path)) {
    const char *name = strrchr(path, '/');
    snprintf(status, scap, "OBB: %s", name ? name + 1 : path);
    return 0;
  }
  /* The expansion file is the data: without it the game will not start, and
   * an unpacked data/files is not an accepted stand-in (see jsr_obb.c). */
  snprintf(status, scap, "OBB: MISSING");
  snprintf(help, hcap,
           "Copy the game's expansion file (main.7." PORT_PACKAGE ".obb, from\n"
           "the XAPK or from /Android/obb/" PORT_PACKAGE "/) to:\n  " PORT_ROOT_PATH "/obb/\n"
           "\n"
           "The console unpacks it into " PORT_ROOT_PATH "/data/files the first\n"
           "time it runs. The data is 1.25 GB, so the card needs about 1.9 GB\n"
           "free: the .obb and the copy of it on the card at once.");
  return 1;
}

void port_launcher_instructions(void) {
  printf("  1. put the APK of your own " PORT_APK_DESC "\n"
         "     (any file name) in " PORT_ROOT_PATH "\n"
         "  2. put its expansion file in " PORT_ROOT_PATH "/obb\n"
         "     (the first start unpacks it into " PORT_ROOT_PATH "/data/files)\n");
}