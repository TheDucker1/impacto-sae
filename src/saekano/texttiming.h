#pragma once

#include "formats.h"

#include <array>

namespace Impacto::Saekano {
struct VoiceTextDurations {
  uint32_t Selected = 0, Reference = 0;
};
// env_config prefix, shared by voice-synchronized text consumers. The original
// option indexes this authored vector; reference timing uses element2.
struct TextTimingEnvironment {
  std::array<uint8_t, 3> Flags{};
  std::vector<float> Factors;
  std::vector<uint32_t> Intervals;
  size_t PrefixBytes = 0;
  size_t IntervalPrefixBytes = 0;
  static TextTimingEnvironment Decode(std::span<const uint8_t> bytes,
                                      bool readIntervals = false);
  VoiceTextDurations ForVoice(uint32_t duration, uint8_t option) const;
  uint32_t ForCharacters(uint8_t option) const;
};
}  // namespace Impacto::Saekano
