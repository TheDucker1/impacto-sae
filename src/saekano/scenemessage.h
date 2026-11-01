#pragma once

#include "headlessbackend.h"
#include "ordinaryvoice.h"
#include "resources.h"
#include <functional>

namespace Impacto::Saekano {
// Explicit fresh settings7350 only. Never apply on manager reset or restore.
void InitializeFreshMessageOwner(SemanticState& owner);
// Actor selection/lip binding is supplied by the owning actor service. Do not
// infer actor indices directly from speaker IDs in this text backend.
class OrdinaryMessageMouth {
 public:
  virtual ~OrdinaryMessageMouth() = default;
  virtual void Begin(int32_t speaker, const ProfileMouthAnimation& envelope,
                     uint64_t now) = 0;
  virtual void End(int32_t speaker) = 0;
};
struct SceneMessageVoiceOwner {
  OrdinaryVoiceOutput& Output;
  OrdinaryMessageMouth& Mouth;
  OrdinaryVoiceQueue Queue;
  std::vector<ScriptVoiceEnvelope> Envelopes;
  std::vector<OrdinaryVoiceRange> Ranges;
  OrdinaryVoiceSettings Settings;
  std::optional<OrdinaryVoiceQueue> SecondaryQueue;
};
// Native battle-select Start stops base+NextSlot on both retained contexts.
// Keep the external-stop cache invalidation used by normal message cleanup.
void StopSceneDialogueNextVoiceSlots(SceneMessageVoiceOwner& owner);
// Native result Start: immediate stop (AAStop flag1), both slots of both owners.
void StopSceneDialogueVoicesImmediately(SceneMessageVoiceOwner& owner);
void PrepareSceneMemoryVoice(SceneMessageVoiceOwner& owner, uint32_t channel, Bytes encoded);
void StopSceneDialogueVoice(SceneMessageVoiceOwner& owner,uint32_t channel);
// Call after an external shared audio stop has retired all physical handles.
// Preserve catalog/allocation and actor/mouth state; only handle caches expire.
void InvalidateSceneVoicePreparations(SceneMessageVoiceOwner& owner);
struct SceneMessageTextState {
  size_t Command = 0;
  size_t Side = 0;
  OrdinaryMessageText Parsed;
  VoiceTextReveal Reveal;
  uint64_t Epoch = 0;
  uint32_t SourceUnits = 0;
  bool PreviousUpdated = false;
  int32_t VoiceId = -1, Speaker = -1;
  int32_t BacklogVoiceGroup = -1;
  std::optional<uint32_t> VoiceDuration;
  OrdinaryVoiceGate VoiceGate{-2, -1};
  int32_t AudioPosition = -1;
  std::optional<uint64_t> LastRevealSample;
};
// Shared frontend text lifecycle with two retained native context containers.
// Selector0/policy0 and normal voiced updates are bound. Async, append and
// voiced skip routes remain explicit unsupported boundaries.
// Font/layout, input polling and audio services belong to the owning frontend.
class SceneMessageBackend : public HeadlessBackend {
 public:
  std::function<void(uint32_t, uint64_t, int32_t)> ActivateLane;
  SceneMessageBackend(const TextFormatEnvironment& format,
                      const TextTimingEnvironment& timing, uint8_t textSpeed,
                      std::optional<uint8_t> autoSpeed = std::nullopt);
  // The VM owns typed result entry when ResultHost/config are bound. Direct
  // presentation fallback stays rejected, including the still-unbound PC host.
  bool Supports(uint16_t opcode) const override { return opcode==0x3f8 || HeadlessBackend::Supports(opcode); }
  void StartPresentation(size_t command,uint16_t opcode,std::span<const uint8_t> payload,bool lane,uint64_t now) override;
  void BeginMessage(size_t command, const MessageCommand& message, size_t side,
                    uint64_t now) override;
  std::optional<int32_t> MessageVoiceGroup(size_t command) const override;
  int32_t MessageBacklogVoice(size_t command) const override;
  void SetSplitDialogue(bool split) override { splitDialogue_=split; }
  void SetMessageDualCleanup(bool enabled) override { dualCleanup_=enabled; }
  void ClearMessage(size_t side) override;
  bool MessageRevealed(size_t command, uint64_t now) override;
  void RevealMessage(size_t command) override;
  bool MessageAdvanceAllowed(size_t command, uint64_t now) override;
  void EndMessageUpdate(size_t command, uint64_t now) override;
  bool BeginMessageUpdate(size_t command, uint64_t now, UpdateRoute route) override;
  void CompleteMessage(size_t command, uint64_t now) override;
  void BindVoiceOwner(SceneMessageVoiceOwner& owner);
  void SetPreferences(uint8_t textSpeed,std::optional<uint8_t> autoSpeed);
  bool ConsumeVoiceEvent(const GameEvent& event);
  void UpdateMessage(uint64_t now);
  bool CursorVisible() const { return cursorVisible_; }
  const SceneMessageTextState* CurrentMessage() const;
  const SceneMessageTextState* MessageSide(size_t side) const;
  void ResetMessages();  // Owning frontend calls this on manager replacement.

 private:
  const TextFormatEnvironment& format_;
  uint32_t interval_;
  TextTimingEnvironment timing_;
  uint8_t textSpeed_;
  std::optional<uint8_t> autoSpeed_;
  uint64_t sample_ = 0;
  bool cursorVisible_ = false;
  std::array<std::optional<SceneMessageTextState>,2> messages_;
  SceneMessageTextState* current_=nullptr;
  bool splitDialogue_=false;
  bool dualCleanup_=false;
  OrdinaryVoiceQueue& VoiceQueue(size_t side);
  SceneMessageVoiceOwner* voices_ = nullptr;
};
}  // namespace Impacto::Saekano
