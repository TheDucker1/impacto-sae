#include "scenemessage.h"

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
                                         uint8_t textSpeed)
    : format_(format), interval_(timing.ForCharacters(textSpeed)) {
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
const SceneMessageTextState* SceneMessageBackend::CurrentMessage() const {
  return current_ ? &*current_ : nullptr;
}
void SceneMessageBackend::ResetMessages() {
  current_.reset();
  sample_ = 0;
  cursorVisible_ = false;
}
}  // namespace Impacto::Saekano
