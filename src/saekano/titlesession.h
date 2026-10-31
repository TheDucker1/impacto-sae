#pragma once
#include "formats.h"
#include "profile.h"

namespace Impacto::Saekano {
struct SemanticState;
// Owner5e814..5e82f. Neutral names retain the native reset contract until their
// individual consumers are traced. Native padding is not an engine field.
struct TitleSessionFields {
  std::array<uint8_t,3> Flags{};
  std::array<int32_t,6> Words{};
};
struct TitleSessionSeeds {
  std::array<uint8_t,3> KeywordFlags{};
  std::vector<int32_t> InitialKeywords;
  size_t KeywordPrefixBytes=0;
  ProfileInitialStats Stats;
  static TitleSessionSeeds Decode(std::span<const uint8_t> keyword,std::span<const uint8_t> profile);
};
// Native title phase0 and New Game reset only working session state. Persistent
// Bank1, preferences, histories, profile categories/cache and read marks survive.
void ResetTitleSession(SemanticState& owner,const TitleSessionSeeds& seeds);
}
