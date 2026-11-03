#pragma once
#include "formats.h"
#include <array>
namespace Impacto::Saekano {
// Proven env_global prefix through c998. Later fields have separate readers.
struct GlobalUiEnvironment {
  std::array<uint8_t,3> Flags{};
  uint32_t TextHeight=0,FadeDuration=0;
  size_t PrefixBytes=0;
  static GlobalUiEnvironment Decode(std::span<const uint8_t> bytes);
};
}
