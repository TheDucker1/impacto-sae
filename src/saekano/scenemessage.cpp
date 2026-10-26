#include "scenemessage.h"
#include <limits>

namespace Impacto::Saekano {
void InitializeFreshMessageOwner(SemanticState& owner) {
  if (owner.WindowOpacity || owner.ReadColorEnabled)
    throw UnsupportedCommand(
        "fresh message settings would overwrite known owner state");
  owner.WindowOpacity = 10;
  owner.ReadColorEnabled = true;
}
SceneMessageBackend::SceneMessageBackend(const TextFormatEnvironment& format,
                                         const TextTimingEnvironment& timing,
                                         uint8_t textSpeed,
                                         std::optional<uint8_t> autoSpeed)
    : format_(format), interval_(timing.ForCharacters(textSpeed)),
      timing_(timing), textSpeed_(textSpeed), autoSpeed_(autoSpeed) {
  if (autoSpeed_) timing_.ForAutoDelay(*autoSpeed_);
  AutoReveal = AutoAdvance = false;
}
void SceneMessageBackend::BeginMessage(size_t command,
                                       const MessageCommand& message,
                                       size_t side, uint64_t now) {
  if (side || message.Route || message.Selector || message.VoiceId >= 0 ||
      message.CompletionPolicy)
    throw UnsupportedCommand(
        "ordinary scene voice/async/selector/append/split text route is not "
        "implemented");
  SceneMessageTextState next;
  next.Command = command;
  if (message.Text.size() > std::numeric_limits<uint32_t>::max())
    throw FormatError("ordinary message source exceeds native length");
  next.SourceUnits = uint32_t(message.Text.size());
  next.Epoch = now;
  next.Parsed = SplitOrdinaryMessageText(message.Text, format_);
  next.Reveal.PrepareUtf16(next.Parsed.Body, format_, now);
  next.Reveal.StartCharacters(interval_, now);
  if (AutoReveal) next.Reveal.Reveal(now);
  current_ = std::move(next);
  sample_ = now;
  cursorVisible_ = false;
}
void SceneMessageBackend::UpdateMessage(uint64_t now) {
  if (!current_) return;
  current_->Reveal.Update(now);
  sample_ = now;
}
bool SceneMessageBackend::MessageRevealed(size_t command, uint64_t now) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  UpdateMessage(now);
  if (current_->Reveal.Revealed) cursorVisible_ = true;
  return current_->Reveal.Revealed;
}
void SceneMessageBackend::RevealMessage(size_t command) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  current_->Reveal.Reveal(sample_);
}
bool SceneMessageBackend::MessageAdvanceAllowed(size_t command, uint64_t now) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  if (!AutoAdvance) return false;
  if (!autoSpeed_)
    throw UnsupportedCommand("scene message auto speed is unknown");
  MessageAutoTiming message;
  message.AutoEnabled = AutoAdvance;
  message.PreviousUpdated = current_->PreviousUpdated;
  message.TextRevealed = current_->Reveal.Revealed;
  message.SourceUnits = current_->SourceUnits;
  message.Epoch = current_->Epoch;
  message.TextSpeed = textSpeed_;
  message.AutoSpeed = *autoSpeed_;
  return MessageAutoReady(timing_, message, now);
}
void SceneMessageBackend::EndMessageUpdate(size_t command, uint64_t) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  current_->PreviousUpdated = true;
}
const SceneMessageTextState* SceneMessageBackend::CurrentMessage() const {
  return current_ ? &*current_ : nullptr;
}
void SceneMessageBackend::ResetMessages() {
  current_.reset();
  sample_ = 0;
  cursorVisible_ = false;
}
}  // namespace Impacto::Saekano
