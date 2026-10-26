#include "skipauto.h"
#include "commands.h"

#include <bit>

namespace Impacto::Saekano {
void UpdateSkipAutoPolicy(SemanticState& owner, SkipAutoState& policy,
                          const SkipAutoInput& input, int32_t autoKeyDuration,
                          uint64_t now) {
  if (autoKeyDuration < 0) throw FormatError("negative auto key duration");
  auto next = policy;
  auto mode = owner.SkipMode, phase = owner.SkipPhase;
  const bool blocked = input.HideMessages || input.ServicesBlocked;
  const bool eligible = owner.SkipUnreadEnabled != 0 || input.HistoryGate;
  bool heldEligible = false, resetEffects = false;
  if (!blocked) {
    if (mode != 2) {
      mode = input.SkipHeld ? 1 : 0;
      heldEligible = input.SkipHeld && eligible;
    }
    const bool gesture =
        input.PrimaryFlick &&
        (mode == 2 ? input.PrimaryVelocityX < 0 : input.PrimaryVelocityX > 0);
    if (input.SkipPressed || input.SkipButtonReleased || gesture) {
      if (mode == 2) {
        mode = 0;
        next.SkipIndicators = {false, false};
      } else
        mode = 2;
    }
    if ((mode == 2 && eligible) || heldEligible) {
      if (!owner.Effects.Known)
        throw UnsupportedCommand("skip policy common effect state is missing");
      resetEffects = true;
      next.Auto = false;
      if (phase == 0) {
        phase = 1;
        next.SkipIndicators = {true, true};
        next.SkipStartedAt = now;
      }
    } else {
      phase = 0;
      next.SkipIndicators = {mode != 0, mode != 0};
    }
  } else {
    phase = 0;
    next.SkipIndicators = {mode != 0, mode != 0};
  }
  if (phase == 3) {
    phase = 1;
    next.Auto = false;
    next.SkipIndicators = {true, true};
  }
  if (!blocked) {
    bool allowButton = true;
    if (input.AutoPressed) {
      next.AutoKeyTimer.Start(0, autoKeyDuration, autoKeyDuration, now);
      next.AutoKeyPlaying = true;
      allowButton = false;
    }
    if (input.AutoHeld) {
      if (next.AutoKeyPlaying) next.AutoKeyTimer.Tick(now);
      if (next.AutoKeyTimer.Finished()) {
        next.Auto = !next.Auto;
        next.AutoKeyTimer.Start(0, autoKeyDuration, autoKeyDuration, now);
        next.AutoKeyPlaying = false;
      }
    } else if (allowButton &&
               (input.AutoButtonReleased || input.PrimaryFullHold)) {
      next.Auto = !next.Auto;
    }
  }
  next.AutoIndicator = next.Auto;
  owner.SkipMode = mode;
  owner.SkipPhase = phase;
  if (resetEffects) {
    owner.Effects.CurrentValue = owner.Effects.RestoreValue;
    for (uint32_t i = 0; i < 2; ++i)
      owner.PendingEvents.push_back(
          {GameEventKind::StopChannel,
           std::bit_cast<int32_t>(uint32_t(owner.Effects.FirstAudioChannel) +
                                  i)});
  }
  policy = next;
}
}  // namespace Impacto::Saekano
