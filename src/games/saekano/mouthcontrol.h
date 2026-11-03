#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace Impacto::Saekano {
struct OrdinaryMouthControl {
  bool OverrideEnabled;
  std::array<int16_t, 5> OverrideIds;
  bool DualCleanup;
  std::optional<std::array<int32_t, 2>> CleanupIds;
};
OrdinaryMouthControl MakeFreshOrdinaryMouthControl();
}
