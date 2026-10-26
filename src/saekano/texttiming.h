#pragma once

#include "formats.h"

#include <array>
#include <optional>

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
  std::vector<int32_t> AutoDelays;
  size_t PrefixBytes = 0;
  size_t IntervalPrefixBytes = 0;
  size_t AutoDelayPrefixBytes = 0;
  static TextTimingEnvironment Decode(std::span<const uint8_t> bytes,
                                      bool readIntervals = false,
                                      bool readAutoDelays = false);
  VoiceTextDurations ForVoice(uint32_t duration, uint8_t option) const;
  uint32_t ForCharacters(uint8_t option) const;
  int32_t ForAutoDelay(uint8_t option) const;
};
// Original source units include the speaker/markup, before body parsing.
// Voice duration lookup and owning update/reveal gates are caller boundaries.
struct MessageAutoTiming {
  bool AutoEnabled = false, PreviousUpdated = false, TextRevealed = false;
  int32_t VoiceId = -1;
  std::optional<uint32_t> VoiceDuration;
  uint32_t SourceUnits = 0;
  uint8_t TextSpeed = 0, AutoSpeed = 0;
  uint64_t Epoch = 0;
};
bool MessageAutoReady(const TextTimingEnvironment& environment,
                      const MessageAutoTiming& message, uint64_t now);
}  // namespace Impacto::Saekano
