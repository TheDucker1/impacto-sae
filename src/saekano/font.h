#pragma once

#include "image.h"

namespace Impacto::Saekano {
struct GsfGlyph {
  uint16_t Atlas = 0, X = 0, Y = 0;
  uint8_t Width = 0, Height = 0;
};
struct GsfFont {
  std::vector<Bytes> Atlases;
  std::map<char16_t, GsfGlyph> Glyphs;
  std::u16string Characters;
  uint8_t BitmapHeight = 0;
  // The game initializes PVF first and loads the GSF variant with a final
  // UTF16 character-list section, in addition to atlas and metric sections.
  static GsfFont Decode(std::span<const uint8_t> bytes);
};
// Native800b2ba0's PVF advance is fixed half/full width, not the TTF hmtx.
uint32_t PvfGlyphAdvance(char16_t character, uint32_t pixelSize);
struct GsfGlyphDraw {
  uint16_t Atlas = 0;
  std::array<int32_t, 4> Destination{}, Source{};
};
// Native800b2f30 cached bitmap override; a missing glyph falls through to PVF.
std::optional<GsfGlyphDraw> ResolveGsfGlyph(const GsfFont& font,
                                            char16_t character, int32_t x,
                                            int32_t y, uint32_t pixelSize);
}  // namespace Impacto::Saekano
