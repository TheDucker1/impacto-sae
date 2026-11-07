#include "profilemode.h"
#include <algorithm>

namespace Impacto::Saekano {
void InitializeFreshProfileEntry(
    SemanticState& state, const ProfileInitialStats& stats,
    std::span<const ProfileItemCharacterTable> catalog) {
  if (state.ProfileOwner.Known || state.ProfileOwner.ResultCategories ||
      state.ProfileOwner.CachedOrdinal != 0 ||
      std::any_of(state.ProfileOwner.Stats.begin(), state.ProfileOwner.Stats.end(),
                  [](int32_t value) { return value != 0; }) ||
      state.TextSpeed || state.AutoSpeed || state.LastBackgroundId)
    throw FormatError("fresh profile entry would overwrite existing owner state");
  // Build before publishing, so malformed authored data leaves state untouched.
  auto owner = MakeFreshProfileOwner(stats, catalog);
  state.ProfileOwner = std::move(owner);
  state.TextSpeed = 2;          // Owner217f6 -> settings7350, S6022c byte0.
  state.AutoSpeed = 2;          // Same constructor, S6022d byte1.
  state.LastBackgroundId = -1;  // Owner216c4, S5e830.
}

ProfileMode::ProfileMode(SemanticState& state, ProfileServices& services,
                         const ProfileEnvironment& environment,
                         const ModeResultEnvironment& shared, uint64_t now)
    : services_(services), environment_(environment), shared_(shared) {
  if (!state.ProfileOwner.Known || !state.ProfileOwner.ResultCategories ||
      !state.TextSpeed || !state.LastBackgroundId)
    throw UnsupportedCommand(
        "profile mode requires initial owner, categories, text speed and "
        "background");
  wave_.Begin(environment.WavePeriod, now);
  services_.BeginMenu(state.OtakuMode, *state.TextSpeed);
  // Outer80030566..3063c constructs a fresh mode and clears owner7378c.
  state.DayEnabled = 0;
}
int32_t ProfileMode::Phase() const {
  switch (stage_) {
    case Stage::Loading:
      return 2;
    case Stage::Selection:
      return selection_.Phase;
    case Stage::Result:
      return result_.Phase;
    case Stage::Response:
      return response_.Phase;
    case Stage::Complete:
      return 20;
  }
  throw FormatError("invalid owning profile stage");
}
bool ProfileMode::Update(SemanticState& state,
                         const ProfileSelectionInput& input, uint64_t now) {
  if (stage_ == Stage::Complete) return true;
  const bool done = UpdatePhases(state, input, now);
  if (!done) {
    wave_.Update(now);
    if (arrows_.Started) arrows_.Update(now);
    if (navigation_.Started) navigation_.Update(now);
  }
  return done;
}
bool ProfileMode::UpdatePhases(SemanticState& state,
                               const ProfileSelectionInput& input,
                               uint64_t now) {
  if (stage_ == Stage::Loading) {
    if (!services_.MenuReady()) return false;
    arrows_.Begin(environment_.ArrowTravel, environment_.ArrowDuration, now);
    navigation_.Begin(environment_.Navigation.Travel,
                      environment_.Navigation.Duration, now);
    BeginProfileSelection(selection_, state.ProfileOwner, services_.Scene());
    stage_ = Stage::Selection;
  }
  if (stage_ == Stage::Selection) {
    const bool done =
        UpdateProfileSelection(selection_, environment_, input, now);
    for (const auto sound : selection_.UiSounds)
      state.PendingEvents.push_back({GameEventKind::Sound, sound});
    if (!done) return false;
    BeginProfileResult(result_, services_.Scene(), selection_.Ordinal,
                       selection_.Selectors[selection_.Ordinal]);
    stage_ = Stage::Result;
  }
  if (stage_ == Stage::Result) {
    const bool done =
        UpdateProfileResult(state, result_, environment_, shared_,
                            {false, input.Accept, &services_}, now);
    for (const auto sound : result_.UiSounds)
      state.PendingEvents.push_back({GameEventKind::Sound, sound});
    if (!done) return false;
    BeginProfileResponse(state, response_, result_);
    stage_ = Stage::Response;
  }
  auto responseInput = services_.ResponseStatus(now);
  responseInput.Accept = input.Accept;
  const bool done = UpdateProfileResponse(
      state, response_, environment_, services_.Scripts(), responseInput, now);
  for (const auto action : response_.Actions) {
    if (action == ProfileResponseAction::StopBgm)
      state.PendingEvents.push_back({GameEventKind::StopBgm, 0});
    else
      services_.Act(action, response_, now);
  }
  for (const auto sound : response_.UiSounds)
    state.PendingEvents.push_back({GameEventKind::Sound, sound});
  if (done) stage_ = Stage::Complete;
  return done;
}
}  // namespace Impacto::Saekano
