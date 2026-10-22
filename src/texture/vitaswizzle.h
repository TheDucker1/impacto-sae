#pragma once

#include <bit>
#include <cstdint>

namespace Impacto::TexLoad {
// Existing Impacto Vita texture order, shared by GPU and headless readers.
// Thanks @xdanieldzd, @FireyFly and ryg:
// https://github.com/xdanieldzd/Scarlet/blob/d8aabf430307d35a81b131e40bb3c9a4828bdd7b/Scarlet/Drawing/ImageBinary.cs
// http://xen.firefly.nu/up/rearrange.c.html
// https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
inline uint32_t VitaCompactBits(uint32_t x) {
  x &= 0x55555555;
  x = (x ^ (x >> 1)) & 0x33333333;
  x = (x ^ (x >> 2)) & 0x0f0f0f0f;
  x = (x ^ (x >> 4)) & 0x00ff00ff;
  return (x ^ (x >> 8)) & 0x0000ffff;
}
inline void VitaUnswizzle(int* x, int* y, int width, int height) {
  if (width == 0) width = 16;
  if (height == 0) height = 16;
  const uint32_t i = uint32_t(*y * width + *x);
  const uint32_t minimum = uint32_t(width < height ? width : height);
  const auto k = std::bit_width(minimum) - 1;
  const auto even = VitaCompactBits(i), odd = VitaCompactBits(i >> 1);
  const auto block = i >> (2 * k) << (2 * k);
  if (height < width) {
    const auto j = block | (odd & (minimum - 1)) << k | (even & (minimum - 1));
    *x = int(j / height);
    *y = int(j % height);
  } else {
    const auto j = block | (even & (minimum - 1)) << k | (odd & (minimum - 1));
    *x = int(j % width);
    *y = int(j / width);
  }
}
}  // namespace Impacto::TexLoad
