#pragma once

#include "live2dstate.h"

namespace Impacto::Saekano {
struct SemanticState;

// The non-repeating 56-byte ScriptTween path used by the gauge commands.
// Distinct from the 40-byte model parameter tween. Native arithmetic wraps its
// 32-bit product before signed division and clamps the sample at the deadline.
struct ScriptLinearTween {
  int32_t From = 0, To = 0, Current = 0, Duration = 0;
  uint64_t Epoch = 0, Sample = 0;
  void Start(int32_t from, int32_t to, int32_t duration, uint64_t now);
  void Tick(uint64_t now);
  bool Finished() const;
};

struct BattleGaugeEnvironment {
  int32_t Minimum = 0, NeutralThreshold = 0, Duration = 0;
  int32_t DisplayPerPoint = 0, XPerPoint = 0;
  float ScalePerPoint = 0;
  static BattleGaugeEnvironment Decode(std::span<const uint8_t> bytes);
};

struct BattleGaugeState {
  // SAS1 omitted these fields. A fresh mode must initialize them; restored
  // callers must provide the actual initial capture, not infer from score.
  bool Known = false;
  int32_t Display = 0;  // C+14ad8, separate from logical C+14a58.
  std::array<int32_t, 2> SideCharacters{};  // C+20b4/20bc.
  std::array<int32_t, 2> Standings{};       // C+14ec0/14ec4.
};
struct BattleGaugeCommand {
  int32_t Character = 0, Delta = 0;
  static BattleGaugeCommand Decode(std::span<const uint8_t> payload);
};
struct BattleGaugeAnimation {
  struct Actor {
    ScriptLinearTween X, Scale, Y;
    int32_t BaselineY = 0;
  };
  bool Started = false;
  int32_t ActualDelta = 0;
  ScriptLinearTween Display;
  std::array<std::optional<Actor>, 2> Actors;
};

void StartBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                      const BattleGaugeCommand& command,
                      const BattleGaugeEnvironment& environment,
                      Live2dDriver* models, uint64_t now);
bool UpdateBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                       Live2dDriver* models, bool skipping, uint64_t now);
}  // namespace Impacto::Saekano
