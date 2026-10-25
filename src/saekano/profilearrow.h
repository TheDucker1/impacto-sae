#pragma once

#include <array>
#include <cstdint>

namespace Impacto::Saekano {
// Native profile3d0: stock ease curve, endpoint reversal and previous-sample
// publication to the two widgets. This is not clock-modulo oscillation.
struct ProfileArrowMotion {
  bool Started = false;
  float Current = 0, From = 0, To = 0;
  int32_t Duration = 0;
  uint64_t Epoch = 0;
  int32_t Elapsed = 0;
  std::array<int32_t, 2> Offsets{};
  void Begin(float travel, int32_t duration, uint64_t now);
  void Update(uint64_t now);
};
}  // namespace Impacto::Saekano
