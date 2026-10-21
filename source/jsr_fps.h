#ifndef JSR_FPS_H
#define JSR_FPS_H

#include <stdint.h>

void jsr_fps_reset(void);
void jsr_fps_presented(uint64_t now);
void jsr_fps_draw(int width, int height);

#endif
