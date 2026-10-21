#ifndef JSR_ETC1_H
#define JSR_ETC1_H

#include <stddef.h>
#include <stdint.h>

int jsr_etc1_decode(const uint8_t *data, size_t size, int width, int height,
                    uint8_t *pixels, size_t capacity);

#endif
