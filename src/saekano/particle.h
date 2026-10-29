#pragma once

#include "formats.h"
#include <array>
#include <string>

namespace Impacto::Saekano {
struct ParticleEffectConfiguration {
  std::u16string Name;
  int32_t Parameter = 0;  // Native60-byte record+38; consumer names it later.
};
struct ParticleEnvironment {
  std::array<uint8_t, 3> Flags{};
  float Scalar = 0;  // Native8025ed70; retain without guessing timing units.
  std::vector<ParticleEffectConfiguration> Effects;
  // Native801d45b8: flags, float, counted UTF16/word records.
  static ParticleEnvironment Decode(std::span<const uint8_t> bytes);
};
}  // namespace Impacto::Saekano
