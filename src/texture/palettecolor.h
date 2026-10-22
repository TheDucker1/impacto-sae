#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Impacto::TexLoad {
// GXT ARGB palettes store each color as B,G,R,A bytes. _1RGB forces alpha1.
inline std::array<uint8_t, 4> PaletteColor(std::span<const uint8_t> palette,
                                           uint8_t index, bool hasAlpha) {
  const auto offset = size_t(index) * 4;
  if (offset + 4 > palette.size())
    throw std::invalid_argument("palette color outside GXT palette");
  return {palette[offset + 2], palette[offset + 1], palette[offset],
          hasAlpha ? palette[offset + 3] : uint8_t(255)};
}
}  // namespace Impacto::TexLoad
