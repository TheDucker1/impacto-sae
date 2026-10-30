#pragma once

#include "textreveal.h"

namespace Impacto::Saekano {
struct OrdinaryTextEnvironment {
  OrdinaryMessageEnvironment Window;
  std::array<int32_t, 2> NamePosition{};
  uint32_t DefaultScale = 0;
  std::vector<float> Scales;
  float BodyScale = 0, NameScale = 0;
  int32_t RubyYOffset=0;
  float RubyScale=0;
  std::array<uint8_t,4> RubyShadowColor{};
  std::array<uint8_t, 4> NameColor{}, UnreadColor{}, ReadColor{};
  std::optional<std::array<std::array<uint8_t,4>,2>> KeywordColors;
  size_t PrefixBytes = 0;
  static OrdinaryTextEnvironment Decode(std::span<const uint8_t> message,bool keywords=false);
};
// Current ordinary record's name pass followed by its revealed body glyphs.
// Primary color is supplied by the owning read-state policy, not guessed here.
// Native callback801a8ffc uses255 opacity independently of window opacity.
std::vector<UiTextBlit> BuildOrdinaryMessageTextDraws(
    const OrdinaryTextEnvironment& environment, const OrdinaryMessageText& text,
    const VoiceTextReveal& reveal, const PlainTextLayout& body,
    const TextFormatEnvironment& format, const GsfFont& font,
    uint32_t utilitySize, uint32_t nominalSize, uint32_t openness,
    std::array<int32_t, 2> quake, std::array<uint8_t, 4> primaryColor);
}  // namespace Impacto::Saekano
