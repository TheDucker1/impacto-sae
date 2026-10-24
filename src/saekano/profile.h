#pragma once

#include "formats.h"
#include "gauge.h"

#include <array>
#include <string>

namespace Impacto::Saekano {
struct SemanticState;
struct ProfileEnvironment {
  int32_t IntroFade = 0, Carousel = 0, ResultProgress = 0,
          SecondaryProgress = 0, StatChange = 0;
  size_t PrefixBytes = 0;
  static ProfileEnvironment Decode(std::span<const uint8_t> bytes);
};
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
struct ProfileOwnerState {
  bool Known = false;
  int32_t CachedOrdinal = 0;  // S60108, independent of script Bank0.
  std::array<int32_t, 12>
      Stats{};  // S6010c, four characters, three stats each.
};
struct ProfileStatAnimation {
  bool Started = false;
  int32_t CharacterId = 0;
  std::array<ScriptLinearTween, 3> Changes;
};
size_t InitialProfileOrdinal(const ProfileOwnerState& owner,
                             const ProfileScene& scene);
void StepProfileSelector(uint32_t& selector, bool forward);
const ProfileEvent& SelectedProfileEvent(const ProfileScene& scene,
                                         size_t ordinal, uint32_t selector);
void StartProfileStatChanges(const ProfileOwnerState& owner,
                             ProfileStatAnimation& animation,
                             int32_t characterId, const ProfileEvent& event,
                             int32_t duration, uint64_t now);
void TickProfileStatChanges(ProfileOwnerState& owner,
                            ProfileStatAnimation& animation, uint64_t now);
void FinishProfileStatChanges(ProfileOwnerState& owner,
                              ProfileStatAnimation& animation, uint64_t now);
void RequestProfileCategoryTrophy(SemanticState& state,
                                  const ProfileEvent& event);
void RequestProfileStatTrophies(SemanticState& state, int32_t characterId);
void PublishProfileStats(SemanticState& state, int32_t ordinal);
}  // namespace Impacto::Saekano
