#pragma once

#include <array>
#include <cstdint>
#include "uisprite.h"

namespace Impacto::Saekano {
// Native profile398/3d0: stock ease curve, endpoint reversal and
// previous-sample publication to the two widgets. This is not clock-modulo
// oscillation.
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
struct ProfileSelectionState;
struct ProfileCursorEnvironment {
  int32_t Duration = 0;
  float Travel = 0;
  size_t PrefixBytes = 0;
  static ProfileCursorEnvironment Decode(std::span<const uint8_t> profile);
};
// Native1838 stays dormant until its first revealed/completed UI gate.
struct ProfileCursorMotion {
  ProfileArrowMotion Channel;
  void Update(const ProfileCursorEnvironment& environment, bool ready,
              uint64_t now);
};
std::vector<UiSpriteBlit> BuildProfileArrows(
    const ProfileArrowEnvironment& environment,
    const ProfileSelectionState& selection, const ProfileArrowMotion& motion);
std::vector<UiSpriteBlit> BuildProfileNavigation(
    const ProfileNavigationEnvironment& environment, int32_t phase,
    const ProfileArrowMotion& motion);
}  // namespace Impacto::Saekano
