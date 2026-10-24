#pragma once

#include "gauge.h"
#include "texttiming.h"

#include <string>

namespace Impacto::Saekano {
struct TextFormatEnvironment {
  std::array<std::u16string, 6> Tokens;
  std::vector<std::u16string> CharacterMaps;
  std::array<std::u16string, 4> Following;
  size_t PrefixBytes = 0;
  static TextFormatEnvironment Decode(std::span<const uint8_t> bytes);
};
std::u16string DecodeCp932(std::string_view text);
struct TextGlyphReveal {
  bool Enabled = false;
  ScriptLinearTween Alpha;
};
// The native mode0 path for text with no inline timing/format directives.
// Original profile responses use plain text and the authored line-break marker.
// Font metrics/wrapping are separate; they do not alter this glyph count/order.
struct VoiceTextReveal {
  bool Prepared = false, Started = false, Revealed = false;
  std::u16string Text;
  std::vector<std::u16string> Lines;
  std::vector<TextGlyphReveal> Glyphs;
  VoiceTextDurations Durations;
  float Ratio = 0;
  uint64_t Epoch = 0;
  void Prepare(std::string_view cp932, const TextFormatEnvironment& format,
               uint64_t now);
  void Start(VoiceTextDurations durations, uint64_t now);
  void Update(uint64_t now);
  void Reveal(uint64_t now);
};
}  // namespace Impacto::Saekano
