#pragma once
#include "profilearrow.h"

#include "commands.h"
#include "seekmap.h"
#include "profilewave.h"
#include "profileitems.h"

namespace Impacto::Saekano {
// Explicit fresh-owner operation for profile entry only. Other SemanticState
// fields keep their caller-provided values; this is not complete boot setup.
// Rejects existing profile/settings/background state before making changes.
void InitializeFreshProfileEntry(
    SemanticState& state, const ProfileInitialStats& stats,
    std::span<const ProfileItemCharacterTable> catalog);

// Resource/audio/text gates are serviced by the frontend. Headless decodes
// real assets and consumes PCM on its clock; PC can use the existing audio
// host.
class ProfileServices : public ProfileResultText {
 public:
  virtual void BeginMenu(int32_t scene, uint8_t textSpeed) = 0;
  virtual bool MenuReady() const = 0;
  virtual const ProfileScene& Scene() const = 0;
  virtual const ScriptNameIndex& Scripts() const = 0;
  virtual ProfileResponseInput ResponseStatus(uint64_t now) = 0;
  virtual void Act(ProfileResponseAction action,
                   const ProfileResponseState& response, uint64_t now) = 0;
};

class ProfileMode {
 public:
  enum class Stage { Loading, Selection, Result, Response, Complete };
  ProfileMode(SemanticState& state, ProfileServices& services,
              const ProfileEnvironment& environment,
              const ModeResultEnvironment& shared, uint64_t now = 0);
  bool Update(SemanticState& state, const ProfileSelectionInput& input,
              uint64_t now);
  Stage CurrentStage() const { return stage_; }
  int32_t Phase() const;
  int32_t TargetScript() const { return response_.TargetScript; }
  const ProfileSelectionState& Selection() const { return selection_; }
  const ProfileResultState& Result() const { return result_; }
  const ProfileResponseState& Response() const { return response_; }
  const ProfileWaveAnimation& Wave() const { return wave_; }
  const ProfileArrowMotion& Arrows() const { return arrows_; }
  const ProfileArrowMotion& Navigation() const { return navigation_; }

 private:
  ProfileServices& services_;
  const ProfileEnvironment& environment_;
  const ModeResultEnvironment& shared_;
  Stage stage_ = Stage::Loading;
  ProfileSelectionState selection_;
  ProfileResultState result_;
  ProfileResponseState response_;
  ProfileWaveAnimation wave_;
  ProfileArrowMotion arrows_;
  ProfileArrowMotion navigation_;
  bool UpdatePhases(SemanticState& state, const ProfileSelectionInput& input,
                    uint64_t now);
};
}  // namespace Impacto::Saekano
