#pragma once

#include "vm.h"

namespace Impacto::Saekano {

// A deterministic text backend, sharing the VM's lifecycle with a graphical
// frontend. Presentation commands need a separate implemented backend; this
// class does not claim to draw, decode media or complete unknown opcodes.
class HeadlessBackend : public VmBackend {
 public:
  bool AutoReveal = true;
  bool AutoAdvance = true;
  SceneImageDriver* SceneImages = nullptr;
  SceneImageDriver* Images() override { return SceneImages; }
  CharacterPoseDriver* CharacterPoses = nullptr;
  CharacterPoseDriver* Poses() override { return CharacterPoses; }
  BgmResources* MusicResources = nullptr;
  BgmResources* Music() override { return MusicResources; }
  SeResources* SeArchives = nullptr;
  SeResources* SoundEffects() override { return SeArchives; }
  std::optional<BattleGaugeEnvironment> BattleConfig;
  std::optional<ApproachGaugeEnvironment> ApproachConfig;
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
