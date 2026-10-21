/* jsr_obb_scan.h -- which file in the obb folder is the game's expansion
 * file, whatever the player called it. Shared by the game program (jsr_obb.c)
 * and the launcher (launcher/source/jsr_launcher.c): plain libc, no libnx.
 * MIT.
 *
 * Jet Set Radio's expansion file is main.<version>.<package>.obb (main.7.
 * com.sega.jetsetradio.obb for 1.07), and the XAPK stores it under the same
 * name. Any *.obb in the folder will do: the newest by name is taken, and a
 * patch.* one is reported alongside so the launcher can say it was ignored.
 */
#ifndef JSR_OBB_SCAN_H
#define JSR_OBB_SCAN_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static inline int jsr_obb_is_patch(const char *n) { return strncasecmp(n, "patch", 5) == 0; }

/* 1 and dir/<name> in out when the folder holds an expansion file for the
 * role (0 main, 1 patch). */
static inline int jsr_obb_scan(const char *dir, int patch, char *out, size_t cap) {
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  char best[256] = "";
  struct dirent *e;
  while ((e = readdir(d))) {
    const char *n = e->d_name;
    size_t l = strlen(n);
    if (l < 5 || l >= sizeof best || n[0] == '.' || strcasecmp(n + l - 4, ".obb"))
      continue;
    if (jsr_obb_is_patch(n) != (patch != 0))
      continue;
    if (!best[0] || strcmp(n, best) > 0) /* the newest by name */
      memcpy(best, n, l + 1);
  }
  closedir(d);
  if (!best[0])
    return 0;
  snprintf(out, cap, "%s/%s", dir, best);
  return 1;
}

#endif /* JSR_OBB_SCAN_H */