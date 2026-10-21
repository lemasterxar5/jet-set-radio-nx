/* jsr_libc.c -- the two wide-character shims this build of Jet Set Radio
 * needs, which bionic and newlib do not agree on.
 *
 * libjetsetradio.so reaches for wcschr() and wcscpy() through
 * KATANA/FRAMEWORK/system.cpp and its own font code (XFont). The engine's
 * wide strings are 16-bit either way, so these are the newlib ones under the
 * bionic names the loader asks for. MIT.
 */
#include <stddef.h>
#include <wchar.h>

/* wchar_t: 16-bit on bionic's armeabi-v7a and on devkitARM's arm-none-eabi
 * (the build's ARCH is armv8-a, where wchar_t is 32-bit -- so the signature
 * is spelled out rather than taken from wchar.h, or the loader would bind the
 * wrong thing). */
typedef unsigned short jsr_wchar_t;

jsr_wchar_t *b_wcschr(const jsr_wchar_t *s, jsr_wchar_t c) {
  if (!s)
    return NULL;
  for (; *s; s++)
    if (*s == c)
    return (jsr_wchar_t *)s;
  return NULL;
}

jsr_wchar_t *b_wcscpy(jsr_wchar_t *d, const jsr_wchar_t *s) {
  jsr_wchar_t *r = d;
  if (!d)
    return NULL;
  while (s && (*d++ = *s++))
    ;
  return r;
}