#pragma once

#include "headlessbackend.h"
#include "ordinaryvoice.h"
#include "resources.h"

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
};
struct SceneMessageTextState {
  size_t Command = 0;
  OrdinaryMessageText Parsed;
  VoiceTextReveal Reveal;
  uint64_t Epoch = 0;
  uint32_t SourceUnits = 0;
  bool PreviousUpdated = false;
  int32_t VoiceId = -1, Speaker = -1;
  std::optional<uint32_t> VoiceDuration;
  OrdinaryVoiceGate VoiceGate{-2, -1};
  int32_t AudioPosition = -1;
};
// Shared frontend text lifecycle. This first ordinary route is primary,
// selector0, completion-policy0. Voiced normal updates require an explicitly
// bound voice owner; async/split/voiced skip routes reject explicitly.
// Font/layout, input polling and audio services belong to the owning frontend.
class SceneMessageBackend : public HeadlessBackend {
 public:
  SceneMessageBackend(const TextFormatEnvironment& format,
                      const TextTimingEnvironment& timing, uint8_t textSpeed,
                      std::optional<uint8_t> autoSpeed = std::nullopt);
  void BeginMessage(size_t command, const MessageCommand& message, size_t side,
                    uint64_t now) override;
  bool MessageRevealed(size_t command, uint64_t now) override;
  void RevealMessage(size_t command) override;
  bool MessageAdvanceAllowed(size_t command, uint64_t now) override;
  void EndMessageUpdate(size_t command, uint64_t now) override;
  bool BeginMessageUpdate(size_t command, uint64_t now, UpdateRoute route) override;
  void CompleteMessage(size_t command, uint64_t now) override;
  void BindVoiceOwner(SceneMessageVoiceOwner& owner);
  void UpdateMessage(uint64_t now);
  bool CursorVisible() const { return cursorVisible_; }
  const SceneMessageTextState* CurrentMessage() const;
  void ResetMessages();  // Owning frontend calls this on manager replacement.

 private:
  const TextFormatEnvironment& format_;
  uint32_t interval_;
  TextTimingEnvironment timing_;
  uint8_t textSpeed_;
  std::optional<uint8_t> autoSpeed_;
  uint64_t sample_ = 0;
  bool cursorVisible_ = false;
  std::optional<SceneMessageTextState> current_;
  SceneMessageVoiceOwner* voices_ = nullptr;
};
}  // namespace Impacto::Saekano
