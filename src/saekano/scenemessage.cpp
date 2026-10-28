#include "scenemessage.h"
#include <limits>
#include <algorithm>

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
void SceneMessageBackend::StartPresentation(size_t command,uint16_t opcode,
    std::span<const uint8_t> payload,bool lane,uint64_t now) {
  if(opcode==0x3f8)throw UnsupportedCommand("approach result presentation/audio/input lifecycle is not bound");
  HeadlessBackend::StartPresentation(command,opcode,payload,lane,now);
}
void SceneMessageBackend::BeginMessage(size_t command,
                                       const MessageCommand& message,
                                       size_t side, uint64_t now) {
  if (side || message.Route || message.Selector ||
      message.CompletionPolicy)
    throw UnsupportedCommand(
        "ordinary scene async/selector/append/split text route is not "
        "implemented");
  SceneMessageTextState next;
  next.Command = command;
  if (message.Text.size() > std::numeric_limits<uint32_t>::max())
    throw FormatError("ordinary message source exceeds native length");
  next.SourceUnits = uint32_t(message.Text.size());
  next.Epoch = now;
  next.Speaker = message.SpeakerId;
  next.Parsed = ParseOrdinaryMessageControls(message.Text, format_);
  next.Reveal.PrepareUtf16(next.Parsed.Body, format_, now);
  next.Reveal.ConfigureQueueMarkers(next.Parsed.QueueMarkers);
  if (!next.Parsed.QueueMarkers.empty() && !ActivateLane)
    throw UnsupportedCommand("inline queue lane consumer is not bound");
  const ScriptVoiceEnvelope* envelope = nullptr;
  if (message.VoiceId >= 0) {
    if (!voices_) throw UnsupportedCommand("ordinary voice owner is not bound");
    if (message.VoiceId > INT16_MAX)
      throw FormatError("ordinary voice ID exceeds native signed16 catalog");
    if (OrdinaryVoiceEligible(message.VoiceId, voices_->Ranges, voices_->Settings).Enabled) {
      const auto found = std::find_if(voices_->Envelopes.begin(), voices_->Envelopes.end(),
          [&](const auto& item) { return item.VoiceId == message.VoiceId; });
      if (found == voices_->Envelopes.end() || found->Mouth.Duration < 0)
        throw FormatError("ordinary voice envelope is unavailable");
      envelope = &*found;
      next.VoiceId = message.VoiceId;
      next.VoiceDuration = uint32_t(envelope->Mouth.Duration);
      (void)timing_.ForVoice(*next.VoiceDuration, textSpeed_);
    }
  }
  if (!next.Parsed.QueueMarkers.empty() && !envelope)
    throw UnsupportedCommand("timed inline queue requires an enabled ordinary voice");
  if (envelope) {
    voices_->Output.Update(now);
    voices_->Mouth.End(message.SpeakerId);  // Native setup843d4, before audio replacement.
    next.VoiceGate.Phase = 0;
    next.VoiceGate.Slot = int32_t(PrepareOrdinaryVoice(
        voices_->Queue, int16_t(message.VoiceId), true, voices_->Output));
  } else next.Reveal.StartCharacters(interval_, now);
  if (AutoReveal) next.Reveal.Reveal(now);
  current_ = std::move(next);
  sample_ = now;
  cursorVisible_ = false;
}
void SceneMessageBackend::UpdateMessage(uint64_t now) {
  if (!current_) return;
  if (voices_) voices_->Output.Update(now);
  if (current_->Reveal.Started && current_->LastRevealSample != now) {
    current_->Reveal.Update(now, current_->AudioPosition);
    current_->LastRevealSample = now;
  }
  sample_ = now;
}
bool SceneMessageBackend::MessageRevealed(size_t command, uint64_t now) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  UpdateMessage(now);
  for (const auto lane : current_->Reveal.ConsumeVoiceQueue(current_->AudioPosition)) {
    if (!ActivateLane) throw UnsupportedCommand("inline queue lane consumer was lost");
    ActivateLane(lane, now, current_->AudioPosition);
  }
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
  message.VoiceId = current_->VoiceId;
  message.VoiceDuration = current_->VoiceDuration;
  return MessageAutoReady(timing_, message, now);
}
void SceneMessageBackend::BindVoiceOwner(SceneMessageVoiceOwner& owner) {
  if (current_) throw UnsupportedCommand("cannot replace active message voice owner");
  voices_ = &owner;
}
bool SceneMessageBackend::ConsumeVoiceEvent(const GameEvent& event) {
  if (event.Kind != GameEventKind::StopChannel) return false;
  if (!voices_ || event.Parameter != 0)
    throw UnsupportedCommand("ordinary voice stop owner is missing or fade is unsupported");
  StopOrdinaryVoiceChannel(voices_->Queue, voices_->Output, uint32_t(event.Id));
  return true;
}
bool SceneMessageBackend::BeginMessageUpdate(size_t command, uint64_t now,
                                             UpdateRoute route) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  if (current_->VoiceId < 0) return true;
  if (route != UpdateRoute::Normal)
    throw UnsupportedCommand("ordinary voiced skip/lane update is not bound");
  if (!voices_) throw UnsupportedCommand("ordinary voice owner is unavailable");
  voices_->Output.Update(now);
  struct Gate final : OrdinaryVoiceServices {
    SceneMessageVoiceOwner& Owner;
    SceneMessageTextState& Message;
    const TextFormatEnvironment& Format;
    const TextTimingEnvironment& Timing;
    uint8_t Speed;
    uint64_t Now;
    Gate(SceneMessageVoiceOwner& owner, SceneMessageTextState& message,
         const TextFormatEnvironment& format, const TextTimingEnvironment& timing,
         uint8_t speed, uint64_t now)
        : Owner(owner), Message(message), Format(format), Timing(timing), Speed(speed), Now(now) {}
    bool Ready(uint32_t c) override { return Owner.Output.Ready(c); }
    bool Playing(uint32_t c) override { return Owner.Output.Playing(c); }
    void Start(uint32_t c) override { Owner.Output.Start(c); }
    int32_t Position(uint32_t c) override { return Owner.Output.Position(c); }
    void BeginReveal(OrdinaryVoiceReveal kind) override {
      if (kind == OrdinaryVoiceReveal::Fallback) {
        if (!Message.Parsed.QueueMarkers.empty())
          throw UnsupportedCommand("voiced inline queue fallback is not bound");
        Message.Reveal.PrepareUtf16(Message.Parsed.Body, Format, Now);
        Message.Reveal.StartCharacters(200, Now);
      } else if (kind == OrdinaryVoiceReveal::Voiced) {
        Message.Reveal.Start(Timing.ForVoice(*Message.VoiceDuration, Speed), Now);
        const auto envelope = std::find_if(Owner.Envelopes.begin(), Owner.Envelopes.end(),
            [&](const auto& item) { return item.VoiceId == Message.VoiceId; });
        if (envelope == Owner.Envelopes.end()) throw FormatError("ordinary voice envelope lost");
        Message.Epoch = Now;
        Owner.Mouth.Begin(Message.Speaker, envelope->Mouth, Now);
      } else throw UnsupportedCommand("unexpected unvoiced gate callback");
      Message.Epoch = Now;
    }
  } services(*voices_, *current_, format_, timing_, textSpeed_, now);
  const auto update = UpdateOrdinaryVoice(current_->VoiceGate,
                                          voices_->Queue.BaseChannel, services);
  current_->AudioPosition = update.Position;
  sample_ = now;
  return !update.SkipTextUpdate;
}
void SceneMessageBackend::CompleteMessage(size_t command, uint64_t now) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  if (!voices_) {
    if (current_->VoiceId >= 0)
      throw UnsupportedCommand("ordinary voice owner is unavailable");
    return;
  }
  voices_->Output.Update(now);
  if (current_->VoiceId >= 0 && voices_->Settings.StopOnAdvance && current_->VoiceGate.Slot >= 0)
    voices_->Output.Stop(voices_->Queue.BaseChannel + uint32_t(current_->VoiceGate.Slot));
  voices_->Mouth.End(current_->Speaker);
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
  if (voices_) {
    voices_->Output.Stop(voices_->Queue.BaseChannel);
    voices_->Output.Stop(voices_->Queue.BaseChannel + 1);
    voices_->Queue.Prepared.clear();
    voices_->Mouth.End(current_ ? current_->Speaker : -1);
    voices_ = nullptr;
  }
  current_.reset();
  sample_ = 0;
  cursorVisible_ = false;
}
}  // namespace Impacto::Saekano
