#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>

#include "jsr_etc1.h"
#ifdef JSR_TARGET_PROBE
#include "jsr_target.h"
#endif
#include "rt_cfg.h"
#include "util.h"
#ifdef JSR_SHADER_PROBE
uintptr_t jsr_shader_probe_wrap(const char *name, uintptr_t real);
#endif
#ifdef JSR_SYNC_PROBE
uintptr_t jsr_sync_probe_wrap(const char *name, uintptr_t real);
#endif
#ifdef JSR_VERTEX_PROBE
uintptr_t jsr_vertex_probe_wrap(const char *name, uintptr_t real);
#endif

#define JSR_ETC1_RGB8 0x8d64

static void (*g_compressed_image)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
static void (*g_compressed_subimage)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
static unsigned g_upload_logs, g_decode_logs;

static uint8_t *decode(GLenum format, GLsizei width, GLsizei height, GLsizei size, const void *data) {
  if (format != JSR_ETC1_RGB8 || !rt_config_bool("display", "decode_etc1") ||
      width <= 0 || height <= 0 || size <= 0 || !data ||
      (size_t)width > SIZE_MAX / (size_t)height / 3)
    return NULL;
  size_t capacity = (size_t)width * height * 3;
  size_t blocks_wide = ((size_t)width + 3) / 4;
  size_t blocks_high = ((size_t)height + 3) / 4;
  if (blocks_wide > SIZE_MAX / blocks_high / 8 || (size_t)size != blocks_wide * blocks_high * 8)
    return NULL;
  uint8_t *pixels = malloc(capacity);
  if (!pixels || !jsr_etc1_decode(data, size, width, height, pixels, capacity)) {
    free(pixels);
    debugPrintf("[texture] ETC1 CPU decode failed for %dx%d; using original upload\n", width, height);
    return NULL;
  }
  if (g_decode_logs++ < 8)
    debugPrintf("[texture] ETC1 -> RGB8 on CPU: %dx%d (%u -> %u bytes)\n",
                width, height, (unsigned)size, (unsigned)capacity);
  return pixels;
}

static void compressed_image(GLenum target, GLint level, GLenum format, GLsizei width,
                              GLsizei height, GLint border, GLsizei size, const void *data) {
  if (g_upload_logs++ < 12)
    debugPrintf("[texture] upload format=0x%x level=%d size=%dx%d bytes=%d\n",
                (unsigned)format, level, width, height, size);
  uint8_t *pixels = border || level < 0 ? NULL : decode(format, width, height, size, data);
  if (pixels) {
    GLint alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(target, level, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    free(pixels);
  } else {
    g_compressed_image(target, level, format, width, height, border, size, data);
  }
}

static void compressed_subimage(GLenum target, GLint level, GLint left, GLint top,
                                 GLsizei width, GLsizei height, GLenum format,
                                 GLsizei size, const void *data) {
  uint8_t *pixels = level < 0 ? NULL : decode(format, width, height, size, data);
  if (pixels) {
    GLint alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(target, level, left, top, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    free(pixels);
  } else {
    g_compressed_subimage(target, level, left, top, width, height, format, size, data);
  }
}

uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  if (!real)
    return 0;
  if (!strcmp(name, "glCompressedTexImage2D")) {
    g_compressed_image = (void *)real;
    return (uintptr_t)compressed_image;
  }
  if (!strcmp(name, "glCompressedTexSubImage2D")) {
    g_compressed_subimage = (void *)real;
    return (uintptr_t)compressed_subimage;
  }
#ifdef JSR_TARGET_PROBE
  return jsr_target_wrap(name, real);
#elif defined(JSR_SHADER_PROBE)
  return jsr_shader_probe_wrap(name, real);
#elif defined(JSR_SYNC_PROBE)
  return jsr_sync_probe_wrap(name, real);
#elif defined(JSR_VERTEX_PROBE)
  return jsr_vertex_probe_wrap(name, real);
#else
  return 0;
#endif
}
