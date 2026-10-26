#pragma once

#include "headlessbackend.h"

namespace Impacto::Saekano {
// Explicit fresh settings7350 only. Never apply on manager reset or restore.
void InitializeFreshMessageOwner(SemanticState& owner);
struct SceneMessageTextState {
  size_t Command = 0;
  OrdinaryMessageText Parsed;
  VoiceTextReveal Reveal;
};
// Shared frontend text lifecycle. This first ordinary route is primary,
// unvoiced, selector0, completion-policy0. Other routes reject explicitly.
// Font/layout, input polling and audio services belong to the owning frontend.
class SceneMessageBackend : public HeadlessBackend {
 public:
  SceneMessageBackend(const TextFormatEnvironment& format,
                      const TextTimingEnvironment& timing, uint8_t textSpeed);
  void BeginMessage(size_t command, const MessageCommand& message, size_t side,
                    uint64_t now) override;
  bool MessageRevealed(size_t command, uint64_t now) override;
  void RevealMessage(size_t command) override;
  void UpdateMessage(uint64_t now);
  bool CursorVisible() const { return cursorVisible_; }
  const SceneMessageTextState* CurrentMessage() const;
  void ResetMessages();  // Owning frontend calls this on manager replacement.

 private:
  const TextFormatEnvironment& format_;
  uint32_t interval_;
  uint64_t sample_ = 0;
  bool cursorVisible_ = false;
  std::optional<SceneMessageTextState> current_;
};
}  // namespace Impacto::Saekano
