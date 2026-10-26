#pragma once

#include "gauge.h"

namespace Impacto::Saekano {
struct SemanticState;
// env_global prefix through the authored auto-key hold duration.
struct SkipAutoEnvironment {
  int32_t AutoKeyDuration = 0;
  size_t PrefixBytes = 0;
  static SkipAutoEnvironment Decode(std::span<const uint8_t> bytes);
};
struct SkipAutoInput {
  bool HideMessages = false, ServicesBlocked = false;
  bool HistoryGate =
      false;  // Original read/divergence lookup result, supplied explicitly.
  bool SkipHeld = false, SkipPressed = false, SkipButtonReleased = false;
  bool PrimaryFlick = false;
  float PrimaryVelocityX = 0;
  bool AutoPressed = false, AutoHeld = false, AutoButtonReleased = false;
  bool PrimaryFullHold = false;
};
// Fresh defaults only. Restored callers supply their actual policy/timer state.
struct SkipAutoState {
  bool Auto = false;
  std::array<bool, 2> SkipIndicators{};
  bool AutoIndicator = false;
  std::optional<uint64_t> SkipStartedAt;
  ScriptLinearTween AutoKeyTimer;
  bool AutoKeyPlaying = false;
};
// Original manager policy after its history lookup. Widget queries/key polls
// are inputs; VM message completion, auto delay and renderer rotation are
// separate.
void UpdateSkipAutoPolicy(SemanticState& owner, SkipAutoState& policy,
                          const SkipAutoInput& input, int32_t autoKeyDuration,
                          uint64_t now);
}  // namespace Impacto::Saekano
