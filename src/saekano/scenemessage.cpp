#include "scenemessage.h"
#include <limits>
#include <algorithm>

namespace Impacto::Saekano {
namespace {
void InvalidateVoiceCache(SceneMessageVoiceOwner& owner, uint32_t channel) {
  const auto invalidate=[&](OrdinaryVoiceQueue& q){std::erase_if(q.Prepared,[&](const auto& p){return q.BaseChannel+p.Slot==channel;});};
  invalidate(owner.Queue);
  if(owner.SecondaryQueue)invalidate(*owner.SecondaryQueue);
}
// Invalidate any cache naming a retired physical handle. Bound native owners
// use7/8 and9/10; overlapping explicit restored owners are also handled safely.
struct SharedVoicePreparation final:OrdinaryVoicePreparation {
  SceneMessageVoiceOwner& Owner;
  explicit SharedVoicePreparation(SceneMessageVoiceOwner& owner):Owner(owner){}
  void Stop(uint32_t channel) override {
    InvalidateVoiceCache(Owner, channel);
    Owner.Output.Stop(channel);
  }
  void Prepare(uint32_t channel,int16_t voice)override{Owner.Output.Prepare(channel,voice);}
  void Start(uint32_t channel)override{Owner.Output.Start(channel);}
};
}
void StopSceneDialogueVoicesImmediately(SceneMessageVoiceOwner& owner) {
  const auto valid=[](const OrdinaryVoiceQueue& q){return q.BaseChannel>=7 && q.BaseChannel<=9;};
  if (!owner.SecondaryQueue || !valid(owner.Queue) || !valid(*owner.SecondaryQueue))
    throw UnsupportedCommand("battle result dialogue owners are unavailable");
  SharedVoicePreparation preparation(owner);
  preparation.Stop(owner.Queue.BaseChannel);
  preparation.Stop(owner.Queue.BaseChannel+1);
  preparation.Stop(owner.SecondaryQueue->BaseChannel);
  preparation.Stop(owner.SecondaryQueue->BaseChannel+1);
}
void PrepareSceneMemoryVoice(SceneMessageVoiceOwner& owner, uint32_t channel, Bytes encoded) {
  if (channel<7 || channel>10) throw UnsupportedCommand("memory voice channel outside dialogue7..10");
  owner.Output.PrepareMemory(channel,std::move(encoded));
  InvalidateVoiceCache(owner,channel);
}
void StopSceneDialogueVoice(SceneMessageVoiceOwner& owner,uint32_t channel) {
  if(channel<7 || channel>10)throw UnsupportedCommand("dialogue stop channel outside7..10");
  SharedVoicePreparation(owner).Stop(channel);
}
void StopSceneDialogueNextVoiceSlots(SceneMessageVoiceOwner& owner) {
  if(!owner.SecondaryQueue || owner.Queue.NextSlot>1 || owner.SecondaryQueue->NextSlot>1)
    throw UnsupportedCommand("battle dialogue next-slot owners are unavailable");
  SharedVoicePreparation preparation(owner);
  preparation.Stop(owner.Queue.BaseChannel+owner.Queue.NextSlot);
  preparation.Stop(owner.SecondaryQueue->BaseChannel+owner.SecondaryQueue->NextSlot);
}
OrdinaryVoiceQueue& SceneMessageBackend::VoiceQueue(size_t side) {
  if(!voices_)throw UnsupportedCommand("ordinary voice owner is not bound");
  if(side==0)return voices_->Queue;
  if(side!=1 || !voices_->SecondaryQueue)throw UnsupportedCommand("alternate message voice owner is not bound");
  return *voices_->SecondaryQueue;
}
void InitializeFreshMessageOwner(SemanticState& owner) {
  if (owner.WindowOpacity || owner.ReadColorEnabled || owner.ContinueAutoAfterSelection || owner.ContinueSkipAfterSelection)
    throw UnsupportedCommand(
        "fresh message settings would overwrite known owner state");
  owner.WindowOpacity = 10;
  owner.ReadColorEnabled = true;
  owner.ContinueAutoAfterSelection=true;
  owner.ContinueSkipAfterSelection=true;
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
  if (side>1 || (side && !splitDialogue_) || message.Route || message.Selector ||
      message.CompletionPolicy)
    throw UnsupportedCommand(
        "ordinary scene async/selector/append or unbound split text route is not "
        "implemented");
  SceneMessageTextState next;
  next.Command = command;
  next.Side=side;
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
    auto& queue=VoiceQueue(side);
    if(queue.NextSlot>1 || (dualCleanup_ && (!voices_->SecondaryQueue || voices_->SecondaryQueue->NextSlot>1 || voices_->Queue.NextSlot>1)))
      throw FormatError("invalid split message voice owners");
    voices_->Output.Update(now);
    voices_->Mouth.End(message.SpeakerId);  // Native setup843d4, before audio replacement.
    next.VoiceGate.Phase = 0;
    SharedVoicePreparation preparation(*voices_);
    // Native843e2..84412 stops base+owner34 before preparation. Battle
    // setup stops both owners; prefetched opposite slots remain available.
    if(dualCleanup_) {
      preparation.Stop(voices_->Queue.BaseChannel+voices_->Queue.NextSlot);
      preparation.Stop(voices_->SecondaryQueue->BaseChannel+voices_->SecondaryQueue->NextSlot);
    } else preparation.Stop(queue.BaseChannel+queue.NextSlot);
    next.VoiceGate.Slot = int32_t(PrepareOrdinaryVoice(
        queue, int16_t(message.VoiceId), true, preparation));
  } else next.Reveal.StartCharacters(interval_, now);
  if (AutoReveal) next.Reveal.Reveal(now);
  messages_[side]=std::move(next);current_=&*messages_[side];
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
  const bool automatic=AutoAdvanceEnabled();
  if (!automatic) return false;
  if (!autoSpeed_)
    throw UnsupportedCommand("scene message auto speed is unknown");
  MessageAutoTiming message;
  message.AutoEnabled = automatic;
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
  const auto bound=[&](const OrdinaryVoiceQueue& q){return uint32_t(uint32_t(event.Id)-q.BaseChannel)<=1;};
  if(!bound(voices_->Queue) && (!voices_->SecondaryQueue || !bound(*voices_->SecondaryQueue)))
    throw UnsupportedCommand("external stop outside ordinary voice slots");
  SharedVoicePreparation preparation(*voices_);preparation.Stop(uint32_t(event.Id));
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
                                          VoiceQueue(current_->Side).BaseChannel, services);
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
    SharedVoicePreparation(*voices_).Stop(VoiceQueue(current_->Side).BaseChannel + uint32_t(current_->VoiceGate.Slot));
  voices_->Mouth.End(current_->Speaker);
}
void SceneMessageBackend::EndMessageUpdate(size_t command, uint64_t) {
  if (!current_ || current_->Command != command)
    throw UnsupportedCommand("scene message has not started");
  current_->PreviousUpdated = true;
}
const SceneMessageTextState* SceneMessageBackend::CurrentMessage() const {
  return current_;
}
const SceneMessageTextState* SceneMessageBackend::MessageSide(size_t side) const {
  const auto& message=messages_.at(side);return message?&*message:nullptr;
}
void SceneMessageBackend::ClearMessage(size_t side) {
  if(current_ && current_->Side==side){current_=nullptr;cursorVisible_=false;}
  messages_.at(side).reset();
}
void SceneMessageBackend::ResetMessages() {
  if (voices_) {
    voices_->Output.Stop(voices_->Queue.BaseChannel);
    voices_->Output.Stop(voices_->Queue.BaseChannel + 1);
    if(voices_->SecondaryQueue){voices_->Output.Stop(voices_->SecondaryQueue->BaseChannel);voices_->Output.Stop(voices_->SecondaryQueue->BaseChannel+1);}
    voices_->Queue.Prepared.clear();
    if(voices_->SecondaryQueue)voices_->SecondaryQueue->Prepared.clear();
    voices_->Mouth.End(current_ ? current_->Speaker : -1);
    voices_ = nullptr;
  }
  current_=nullptr;messages_={};splitDialogue_=false;dualCleanup_=false;
  sample_ = 0;
  cursorVisible_ = false;
}
}  // namespace Impacto::Saekano
