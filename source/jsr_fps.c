#include <stdio.h>
#include <stdint.h>
#include <GLES2/gl2.h>

#include "jsr_fps.h"
#include "util.h"

static GLuint g_program;
static GLuint g_buffer;
static int g_attempted;
static uint64_t g_start;
static unsigned g_frames;
static int g_started;
static char g_label[24] = "FPS: ...";

static const unsigned char k_glyphs[][7] = {
    {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},
    {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},
    {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14},
    {14, 17, 17, 15, 1, 1, 14},
    {31, 16, 16, 30, 16, 16, 16},
    {30, 17, 17, 30, 16, 16, 16},
    {15, 16, 16, 14, 1, 1, 30},
    {0, 4, 4, 0, 4, 4, 0},
    {0, 0, 0, 0, 0, 12, 12},
};

void jsr_fps_reset(void) {
  g_started = 0;
  g_frames = 0;
  snprintf(g_label, sizeof g_label, "FPS: ...");
}

void jsr_fps_presented(uint64_t now) {
  if (!g_started || now < g_start) {
    g_started = 1;
    g_start = now;
    g_frames = 0;
    return;
  }
  g_frames++;
  uint64_t elapsed = now - g_start;
  if (elapsed >= 500000000ull) {
    double fps = (double)g_frames * 1e9 / (double)elapsed;
    snprintf(g_label, sizeof g_label, "FPS: %.1f", fps);
    g_start = now;
    g_frames = 0;
  }
}

static GLuint compile_shader(GLenum kind, const char *source) {
  GLuint shader = glCreateShader(kind);
  if (!shader)
    return 0;
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static int prepare(void) {
  if (g_attempted)
    return g_program != 0;
  g_attempted = 1;
  const char *vertex_source =
      "attribute vec3 point; varying lowp float shade;"
      "void main(){gl_Position=vec4(point.xy,0.0,1.0);shade=point.z;}";
  const char *fragment_source =
      "precision mediump float; varying lowp float shade;"
      "void main(){gl_FragColor=vec4(shade,shade,shade,1.0);}";
  GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  GLuint program = 0;
  GLint ok = 0;
  if (vertex && fragment) {
    program = glCreateProgram();
    if (program) {
      glAttachShader(program, vertex);
      glAttachShader(program, fragment);
      glBindAttribLocation(program, 0, "point");
      glLinkProgram(program);
      glGetProgramiv(program, GL_LINK_STATUS, &ok);
    }
  }
  if (vertex)
    glDeleteShader(vertex);
  if (fragment)
    glDeleteShader(fragment);
  if (ok)
    glGenBuffers(1, &g_buffer);
  if (!ok || !g_buffer) {
    if (program)
      glDeleteProgram(program);
    debugPrintf("[fps] overlay initialization failed; gameplay continues\n");
    return 0;
  }
  g_program = program;
  debugPrintf("[fps] on-screen presentation counter initialized\n");
  return 1;
}

static int glyph_index(char character) {
  if (character >= '0' && character <= '9')
    return character - '0';
  switch (character) {
  case 'F': return 10;
  case 'P': return 11;
  case 'S': return 12;
  case ':': return 13;
  case '.': return 14;
  default: return -1;
  }
}

static void rectangle(float *vertices, unsigned *count, float left, float top,
                      float right, float bottom, float shade, int width, int height) {
  float x0 = 2.0f * left / width - 1.0f;
  float x1 = 2.0f * right / width - 1.0f;
  float y0 = 1.0f - 2.0f * top / height;
  float y1 = 1.0f - 2.0f * bottom / height;
  const float points[] = {x0, y0, shade, x0, y1, shade, x1, y0, shade,
                          x1, y0, shade, x0, y1, shade, x1, y1, shade};
  for (unsigned index = 0; index < sizeof points / sizeof points[0]; index++)
    vertices[(*count)++] = points[index];
}

void jsr_fps_draw(int width, int height) {
  if (width <= 0 || height <= 0 || !prepare())
    return;
  float vertices[(1 + 16 * 35) * 18];
  unsigned count = 0;
  unsigned length = 0;
  while (g_label[length] && length < 16)
    length++;
  float scale = height >= 700 ? 3.0f : 2.0f;
  rectangle(vertices, &count, 8, 8, 20 + length * 6 * scale, 20 + 7 * scale,
            0.0f, width, height);
  for (unsigned letter = 0; letter < length; letter++) {
    int glyph = glyph_index(g_label[letter]);
    if (glyph < 0)
      continue;
    for (unsigned row = 0; row < 7; row++) {
      for (unsigned column = 0; column < 5; column++) {
        if (!(k_glyphs[glyph][row] & (16 >> column)))
          continue;
        float left = 14 + (letter * 6 + column) * scale;
        float top = 14 + row * scale;
        rectangle(vertices, &count, left, top, left + scale, top + scale,
                  1.0f, width, height);
      }
    }
  }

  GLint program, buffer, framebuffer, viewport[4];
  GLint enabled, size, type, normalized, stride, attribute_buffer;
  void *pointer;
  GLboolean color_mask[4];
  const GLenum capabilities[] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE,
                                  GL_SCISSOR_TEST, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};
  GLboolean states[sizeof capabilities / sizeof capabilities[0]];
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &normalized);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribute_buffer);
  glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
  for (unsigned index = 0; index < sizeof capabilities / sizeof capabilities[0]; index++) {
    states[index] = glIsEnabled(capabilities[index]);
    glDisable(capabilities[index]);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, height);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glUseProgram(g_program);
  glBindBuffer(GL_ARRAY_BUFFER, g_buffer);
  glBufferData(GL_ARRAY_BUFFER, count * sizeof(float), vertices, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
  glDrawArrays(GL_TRIANGLES, 0, count / 3);

  glBindBuffer(GL_ARRAY_BUFFER, attribute_buffer);
  glVertexAttribPointer(0, size, type, normalized, stride, pointer);
  if (!enabled)
    glDisableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glUseProgram(program);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
  for (unsigned index = 0; index < sizeof capabilities / sizeof capabilities[0]; index++) {
    if (states[index])
      glEnable(capabilities[index]);
  }
}
