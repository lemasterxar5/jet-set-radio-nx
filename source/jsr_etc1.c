#include "jsr_etc1.h"

static const int k_modifiers[8][4] = {
    {2, 8, -2, -8}, {5, 17, -5, -17}, {9, 29, -9, -29}, {13, 42, -13, -42},
    {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183},
};

static int signed_three(unsigned value) {
  return value & 4 ? (int)value - 8 : (int)value;
}

static uint8_t clamp_byte(int value) {
  return value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value;
}

int jsr_etc1_decode(const uint8_t *data, size_t size, int width, int height,
                    uint8_t *pixels, size_t capacity) {
  if (!data || !pixels || width <= 0 || height <= 0 ||
      (size_t)width > SIZE_MAX / (size_t)height / 3)
    return 0;
  size_t blocks_wide = ((size_t)width + 3) / 4;
  size_t blocks_high = ((size_t)height + 3) / 4;
  if (blocks_wide > SIZE_MAX / blocks_high / 8 ||
      size != blocks_wide * blocks_high * 8 || capacity < (size_t)width * height * 3)
    return 0;
  for (size_t block_y = 0; block_y < blocks_high; block_y++) {
    for (size_t block_x = 0; block_x < blocks_wide; block_x++, data += 8) {
      int colors[2][3];
      for (unsigned channel = 0; channel < 3; channel++) {
        if (data[3] & 2) {
          int first = data[channel] >> 3;
          int second = first + signed_three(data[channel] & 7);
          if (second < 0 || second > 31)
            return 0;
          colors[0][channel] = (first << 3) | (first >> 2);
          colors[1][channel] = (second << 3) | (second >> 2);
        } else {
          colors[0][channel] = (data[channel] >> 4) * 17;
          colors[1][channel] = (data[channel] & 15) * 17;
        }
      }
      unsigned tables[] = {data[3] >> 5, (data[3] >> 2) & 7};
      uint32_t selectors = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                           ((uint32_t)data[6] << 8) | data[7];
      for (unsigned row = 0; row < 4 && block_y * 4 + row < (size_t)height; row++) {
        for (unsigned column = 0; column < 4 && block_x * 4 + column < (size_t)width; column++) {
          unsigned half = data[3] & 1 ? row / 2 : column / 2;
          unsigned bit = column * 4 + row;
          unsigned selector = ((selectors >> bit) & 1) | (((selectors >> (bit + 16)) & 1) << 1);
          int modifier = k_modifiers[tables[half]][selector];
          size_t offset = ((block_y * 4 + row) * width + block_x * 4 + column) * 3;
          for (unsigned channel = 0; channel < 3; channel++)
            pixels[offset + channel] = clamp_byte(colors[half][channel] + modifier);
        }
      }
    }
  }
  return 1;
}
