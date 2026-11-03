#pragma once

#include <cstdint>

namespace Impacto::Saekano {
// The profile constructor's0->960 indefinitely repeating script tween.
// Preserves native delta/restart/catch-up behavior, not clock modulo.
struct ProfileWaveAnimation {
  bool Started = false;
  uint32_t Period = 0;
  int32_t Current = 0;
  uint64_t Epoch = 0, Sample = 0;
  void Begin(int32_t period, uint64_t now);
  void Update(uint64_t now);
};
}  // namespace Impacto::Saekano
