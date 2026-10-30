#pragma once

#include "gauge.h"

namespace Impacto::Saekano {
struct SemanticState;
struct HistoryEnvironment {
  uint8_t SuppressReadHistory = 0;
  size_t PrefixBytes = 0;
  uint8_t InitiallyRead = 0; // env_debug8025c24b; startup table fill only.
  static HistoryEnvironment DecodeDebug(std::span<const uint8_t> bytes);
};
struct ReadHistoryBranch {
  uint16_t Endpoint = 0;
  std::vector<uint8_t> Flags;
};
// Explicit known current-file tables. Empty is a real native state, not an
// unknown restore or a fabricated fresh-unread table.
struct ScriptReadHistory {
  std::vector<uint16_t> Segments;  // Low15 endpoint, high bit read.
  std::vector<ReadHistoryBranch> Branches;
};
// Native startup801937f8 on first.dat child4. The initial-read flag is an
// explicit native configuration input (8025c24b), not restored save state.
struct RegisteredReadHistory {
  std::vector<ScriptReadHistory> Files;
  static RegisteredReadHistory Decode(std::span<const uint8_t> zz1,
                                      std::span<const uint8_t> zz2,
                                      std::span<const uint8_t> zz3,
                                      bool initiallyRead);
  static RegisteredReadHistory LoadFirst(Bytes firstDat, bool initiallyRead);
};
bool SkipHistoryEligible(const HistoryEnvironment& environment,
                          std::span<const ScriptRecord> records,
                          uint32_t current, const ScriptReadHistory* history);
void MarkHistoryRead(const HistoryEnvironment& environment,
                      ScriptReadHistory& history, uint32_t command);
// env_global prefix through the authored auto-key hold duration.
struct SkipAutoEnvironment {
  int32_t AutoKeyDuration = 0;
  size_t PrefixBytes = 0;
  static SkipAutoEnvironment Decode(std::span<const uint8_t> bytes);
};
// Fresh mode3 component timer: updated even when its inactive variant draws.
// Native drops cycle overrun and restarts360->0 at this update's clock.
struct OrdinaryAutoRotation {
  bool Started = false;
  ScriptLinearTween Channel;
  void Begin(int32_t duration, uint64_t now);
  void Update(uint64_t now);
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
