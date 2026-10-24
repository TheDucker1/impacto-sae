#pragma once

#include "formats.h"

#include <array>
#include <string>

namespace Impacto::Saekano {
struct ProfileEvent {
  // Event+00..10: two presentation fields and three stat changes. Keep unknown
  // presentation meanings neutral until their actual consumers are traced.
  std::array<int32_t, 5> Values{};
  std::string ScriptName;
  std::array<int32_t, 2> Presentation{};  // Event+4c/50.
  std::string Response;  // Original Shift-JIS bytes, not lossy UTF8 conversion.
  int32_t Tail = 0;      // Event+8c.
};
struct ProfileCharacter {
  int32_t Id = 0;
  // Only authored/written rows are known. Native unused numeric slots are not
  // initialized by its ten-entry constructor; do not fabricate zero entries.
  std::vector<ProfileEvent> Events;
};
using ProfileScene = std::vector<ProfileCharacter>;
ProfileScene DecodeProfileScene(std::span<const uint8_t> bytes);
}  // namespace Impacto::Saekano
