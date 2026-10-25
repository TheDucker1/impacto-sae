#pragma once

#include "formats.h"
#include "gauge.h"
#include "uisprite.h"

#include <array>
#include <string>

namespace Impacto::Saekano {
struct SemanticState;
class ScriptNameIndex;
struct ProfileEnvironment {
  int32_t IntroFade = 0, WavePeriod = 0, Carousel = 0, ResultProgress = 0,
          SecondaryProgress = 0, StatChange = 0;
  ProfileMenuChromeEnvironment Chrome;
  ProfileItemFrameEnvironment ItemFrames;
  ProfileItemContentEnvironment ItemContents;
  std::vector<std::array<int32_t, 2>> DescriptionPositions;
  float DescriptionScale = 0;
  uint32_t DescriptionColumns = 0;
  float ArrowTravel = 0;
  int32_t ArrowDuration = 0;
  ProfileArrowEnvironment Arrows;
  ProfilePortraitEnvironment Portrait;
  ProfileHudEnvironment Hud;
  ProfileNavigationEnvironment Navigation;
  UiSpriteDefinition ResultReflection;
  size_t PrefixBytes = 0;
  std::array<std::vector<std::u16string>, 3> ResultLabels;
  std::u16string ResultFormat;
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
struct ProfileMouthAnimation {
  int32_t Duration = 0;
  std::vector<uint8_t> Samples;
};
ProfileMouthAnimation DecodeProfileMouth(std::span<const uint8_t> bytes);
// Settings application60236*10 -> channel7 percentage, then channel coefficient
// multiplication/division by100. Coefficient is known service state, never
// reconstructed from a captured saved setting alone. Fresh channels use1.
float ProfileVoiceGain(uint8_t option, float channelCoefficient);
struct ProfileInitialStats {
  std::array<int32_t, 12> Values{};
  size_t PrefixBytes = 0;
  // env_otaku_mode's final counted rows, consumed by owner constructor21408.
  static ProfileInitialStats Decode(std::span<const uint8_t> bytes);
};
struct ProfileOwnerState {
  bool Known = false;
  int32_t CachedOrdinal = 0;  // S60108, independent of script Bank0.
  std::array<int32_t, 12>
      Stats{};  // S6010c, four characters, three stats each.
  // Inline owner S73814 + ordinal*1b30 + eventId*e8. Separate from stats;
  // absent in PFO1 and never inferred from the event ID or category field.
  // Owned rows keep repeated VM/state values small on the Windows stack.
  std::optional<std::vector<std::array<int32_t, 30>>> ResultCategories;
};
struct ProfileStatAnimation {
  bool Started = false;
  int32_t CharacterId = 0;
  std::array<ScriptLinearTween, 3> Changes;
};
// The selection interaction after original scene/UI resources have become
// ready (native phases3..9). Result/response and outer ownership are separate.
// Inputs are real UI actions, never an observed destination or event index.
struct ProfileSelectionInput {
  std::optional<size_t> Ordinal;
  std::optional<bool> CharacterDirection, EventDirection;
  bool Accept = false, Cancel = false;
  std::optional<bool> Confirmation;
};
struct ProfileSelectionState {
  std::vector<int32_t> UiSounds;
  bool Started = false, ConfirmationOpen = false;
  int32_t Phase = 3;
  std::optional<int32_t> PendingPhase;
  int32_t ReturnPhase = 5;
  size_t Ordinal = 0, CharacterCount = 0;
  std::array<uint32_t, 4> Selectors{};
  ScriptLinearTween Fade;
  std::array<ScriptLinearTween, 8> CarouselX, CarouselY;
  // Widget offsets retain the final sample for the completion frame, even
  // after the content tweens have been reset and the selector has changed.
  std::array<std::array<int32_t, 2>, 8> FrameOffsets{};
  std::array<bool, 2> ArrowActive{};
};
void BeginProfileSelection(ProfileSelectionState& selection,
                           const ProfileOwnerState& owner,
                           const ProfileScene& scene);
// Returns true only after confirmed phase10 is committed on a later update.
bool UpdateProfileSelection(ProfileSelectionState& selection,
                            const ProfileEnvironment& environment,
                            const ProfileSelectionInput& input, uint64_t now);
std::u16string BuildProfileResultText(const ProfileOwnerState& owner,
                                      const ProfileEnvironment& environment,
                                      int32_t characterId, size_t ordinal,
                                      const ProfileEvent& event);
class ProfileResultText {
 public:
  virtual ~ProfileResultText() = default;
  virtual bool Update(std::u16string_view text, uint64_t now) = 0;
  virtual void Reveal(uint64_t now) = 0;
};
struct ProfileResultInput {
  bool TextRevealed = false, Accept = false;
  ProfileResultText* Text = nullptr;
};
struct ProfileResultState {
  bool Started = false, Visible = false, ResponseMode = false;
  bool SlidesActive = false, CompletionActive = false, TextAlphaActive = false;
  bool TextStarted = false, RevealRequested = false;
  bool CursorTickRequested = false;
  int32_t Phase = 10;
  std::optional<int32_t> PendingPhase;
  int32_t CharacterId = 0;
  size_t Ordinal = 0;
  ProfileEvent Event;
  std::u16string Text;
  ScriptLinearTween Primary, HorizontalOffset, HorizontalAlpha, VerticalOffset,
      VerticalAlpha, EntryCompletion, TextAlpha, ResultProgress,
      SecondaryProgress, Fade;
  ProfileStatAnimation Stats;
  std::vector<int32_t>
      UiSounds;  // Per-update requests to the UI sound service.
};
void BeginProfileResult(ProfileResultState& result, const ProfileScene& scene,
                        size_t ordinal, uint32_t selector);
// Phases10..14 and18/19, stopping at deferred response-resource entry15.
// Text reveal is a real presentation gate; this never publishes final banks
// or pretends response playback/file replacement has completed.
bool UpdateProfileResult(SemanticState& state, ProfileResultState& result,
                         const ProfileEnvironment& environment,
                         const ModeResultEnvironment& shared,
                         const ProfileResultInput& input, uint64_t now);
enum class ProfileResponseAction {
  BeginResources,
  PreparePresentation,
  PrepareVoice,
  BeginTextReveal,
  StartVoice,
  UpdatePresentation,
  RevealText,
  StopVoice,
  StopBgm
};
struct ProfileResponseInput {
  bool ResourcesReady = false, TextRevealed = false, Accept = false;
  // Native800aa88c requires an existing prepared channel whose state is0;
  // absence of a voice object is not a successful stop acknowledgement.
  bool VoiceStopped = false;
};
struct ProfileResponseState {
  bool Started = false, Visible = false, Completed = false;
  bool CursorTickRequested = false;
  int32_t Phase = 15;
  std::optional<int32_t> PendingPhase;
  int32_t CharacterId = 0, Background = -1, TargetScript = -2;
  size_t Ordinal = 0;
  ProfileEvent Event;
  ScriptLinearTween Fade;
  std::vector<ProfileResponseAction> Actions;
  std::vector<int32_t> UiSounds;
  // PreparePresentation uses Event.Presentation[0] pose, [1] face, white tint,
  // opacity255 and X700. Voice/envelope entry is Event.Tail on channel7.
  // Text is the original Event.Response bytes; its consumer owns CP932/reveal.
};
void BeginProfileResponse(const SemanticState& state,
                          ProfileResponseState& response,
                          const ProfileResultState& result);
bool UpdateProfileResponse(SemanticState& state, ProfileResponseState& response,
                           const ProfileEnvironment& environment,
                           const ScriptNameIndex& scripts,
                           const ProfileResponseInput& input, uint64_t now);
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
