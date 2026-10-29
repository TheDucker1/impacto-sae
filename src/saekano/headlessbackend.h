#pragma once

#include "vm.h"
#include "texttiming.h"
#include "textreveal.h"

namespace Impacto::Saekano {

// A deterministic text backend, sharing the VM's lifecycle with a graphical
// frontend. Presentation commands need a separate implemented backend; this
// class does not claim to draw, decode media or complete unknown opcodes.
class HeadlessBackend : public VmBackend {
 public:
  bool AutoReveal = true;
  bool AutoAdvance = true;
  std::function<void(bool)> ParticleReset;
  std::function<void(const GameEvent&)> ParticleRequest;
  void ResetParticles(bool fresh) override { if (ParticleReset) ParticleReset(fresh); }
  bool StartParticleEffect(const GameEvent& event) override {
    if (!ParticleRequest) return false;
    ParticleRequest(event); return true;
  }
  SceneImageDriver* SceneImages = nullptr;
  SceneImageDriver* Images() override { return SceneImages; }
  CharacterPoseDriver* CharacterPoses = nullptr;
  CharacterPoseDriver* Poses() override { return CharacterPoses; }
  ApproachResources* ApproachResourceBank = nullptr;
  ApproachResources* ApproachAssets() override { return ApproachResourceBank; }
  ApproachMissionAudio* MissionVoiceBank=nullptr;
  std::optional<ApproachMissionEnvironment> MissionSettings;
  std::optional<ApproachSelectionEnvironment> SelectionSettings;
  std::optional<ApproachResultEnvironment> ApproachResultSettings;
  ApproachResultHost* ResultConsumer=nullptr;
  const ApproachResultEnvironment* ApproachResultConfig() const override {
    return ApproachResultSettings?&*ApproachResultSettings:nullptr;
  }
  ApproachResultHost* ResultHost() override { return ResultConsumer; }
  const ApproachSelectionEnvironment* SelectionConfig() const override {
    return SelectionSettings?&*SelectionSettings:nullptr;
  }
  ApproachMissionAudio* MissionVoices() override { return MissionVoiceBank; }
  const ApproachMissionEnvironment* MissionConfig() const override {
    return MissionSettings ? &*MissionSettings : nullptr;
  }
  Live2dDriver* SceneModels = nullptr;
  Live2dDriver* Models() override { return SceneModels; }
  BgmResources* MusicResources = nullptr;
  BgmResources* Music() override { return MusicResources; }
  SeResources* SeArchives = nullptr;
  SeResources* SoundEffects() override { return SeArchives; }
  std::optional<BattleGaugeEnvironment> BattleConfig;
  std::optional<ApproachGaugeEnvironment> ApproachConfig;
  std::optional<ChapterEnvironment> ChapterSettings;
  std::optional<ProfileEnvironment> ProfileSettings;
  std::optional<ModeResultEnvironment> ModeResultSettings;
  std::optional<TextTimingEnvironment> TextTimingSettings;
  std::optional<TextFormatEnvironment> TextFormatSettings;
  ProfileServices* ProfileConsumer = nullptr;
  ProfileServices* ProfileHost() override { return ProfileConsumer; }
  const ModeResultEnvironment* ModeResultConfig() const override {
    return ModeResultSettings ? &*ModeResultSettings : nullptr;
  }
  const ProfileEnvironment* ProfileConfig() const override {
    return ProfileSettings ? &*ProfileSettings : nullptr;
  }
  const ChapterEnvironment* ChapterConfig() const override {
    return ChapterSettings ? &*ChapterSettings : nullptr;
  }
  const ApproachGaugeEnvironment* ApproachEnvironment() const override {
    return ApproachConfig ? &*ApproachConfig : nullptr;
  }
  const BattleGaugeEnvironment* BattleEnvironment() const override {
    return BattleConfig ? &*BattleConfig : nullptr;
  }
  void BeginMessage(size_t command, const MessageCommand&, size_t, uint64_t) override {
    revealed_[command] = AutoReveal;
  }
  bool MessageRevealed(size_t command, uint64_t) override { return revealed_.at(command); }
  bool MessageAdvanceAllowed(size_t, uint64_t) override { return AutoAdvance; }
  void RevealMessage(size_t command) override { revealed_.at(command) = true; }

 private:
  std::map<size_t, bool> revealed_;
};

}  // namespace Impacto::Saekano
