#pragma once

#include "formats.h"

#include <array>

namespace Impacto::Saekano {
struct Live2dActorConfig {
  std::array<int32_t, 2> Anchor{};
  int32_t BlinkInterval = 0;
  std::array<int32_t, 3>
      BlinkMotion{};  // Closing, closed, opening, milliseconds.
};
struct Live2dEnvironment {
  std::array<uint8_t, 3> Flags{};  // Retained without inventing UI meanings.
  std::array<int32_t, 2> MouthCycleParts{};
  std::vector<Live2dActorConfig> Actors;
  static Live2dEnvironment Decode(std::span<const uint8_t> bytes);
  const Live2dActorConfig& Actor(int32_t logical) const;
};

// Game-side scalar transitions, not SDK motion/deformation implementations.
struct Live2dIntegerTween {
  bool Enabled = false;
  int32_t From = 0, To = 0, Current = 0;
  int32_t Duration = 0;
  uint64_t Epoch = 0, Sample = 0;
  void Start(int32_t from, int32_t to, int32_t duration, uint64_t now,
             bool enabled = true);
  void Tick(uint64_t now);
  bool Finished() const;
};
struct Live2dFrameAxis {
  float From = 0, To = 0, Step = 0, Current = 0;
  void Target(float to, int32_t transition);
  void Tick();
};
}  // namespace Impacto::Saekano
