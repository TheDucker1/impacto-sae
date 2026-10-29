#include "approachresulttext.h"
#include "resources.h"
#include "approachmission.h"
#include "skipauto.h"
#include <algorithm>

namespace Impacto::Saekano {
void ApproachResultText::Prepare(const ApproachResultCommand::Outcome& outcome,
    std::span<const ScriptVoiceEnvelope> catalog,const TextFormatEnvironment& format,
    const ModeResultVisualEnvironment& environment,const GsfFont& font,
    const TextLayoutMetrics& metrics,uint32_t utilitySize,uint32_t nominalSize,uint64_t now) {
  ApproachResultText next;next.Voice=outcome.Voice;
  if(outcome.Voice>=0) {
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& row){return row.VoiceId==outcome.Voice;});
    if(found==catalog.end() || found->Mouth.Duration<0)
      throw UnsupportedCommand("result voice duration is unavailable");
    next.VoiceDuration=uint32_t(found->Mouth.Duration);
  } else if(outcome.Voice!=-1)throw FormatError("invalid result voice ID");
  next.Reveal.PrepareUtf16(outcome.Text,format,now,true);
  next.Layout=LayoutPlainText(next.Reveal.Text,format,font,metrics,environment.TextWidth,
                              environment.TextScale,utilitySize,nominalSize);
  if(next.Layout.Glyphs.size()!=next.Reveal.Glyphs.size())
    throw UnsupportedCommand("result glyph layout and reveal disagree");
  *this=std::move(next);
}
void ApproachResultText::Begin(const TextTimingEnvironment& timing,uint8_t speed,uint64_t now) {
  if(Voice>=0) {
    if(!VoiceDuration)throw UnsupportedCommand("result voice duration was not prepared");
    Reveal.Start(timing.ForApproachResult(*VoiceDuration,speed),now);
  } else Reveal.StartCharacters(timing.ForCharacters(speed),now);
}
bool ApproachResultText::Update(uint64_t now) {
  // Native result UpdateReveal always receives-1, not the live audio position.
  Reveal.Update(now,-1);return Reveal.Revealed;
}
void ApproachResultText::RevealAll(uint64_t now) { Reveal.Reveal(now); }
ArchiveApproachResultHost::ArchiveApproachResultHost(GameResources& resources,ApproachMissionAudio& audio,
    SkipAutoState& policy,const TextFormatEnvironment& format,const TextTimingEnvironment& timing,
    ModeResultVisualEnvironment visual,const GsfFont& font,TextLayoutMetrics metrics,
    uint32_t utilitySize,uint32_t nominalSize)
    :resources_(resources),audio_(audio),policy_(policy),format_(format),timing_(timing),
     visual_(std::move(visual)),font_(font),metrics_(metrics),utility_(utilitySize),nominal_(nominalSize) {}
void ArchiveApproachResultHost::Tick(uint64_t now) { audio_.Update(now); }
void ArchiveApproachResultHost::Validate(const SemanticState& owner) const {
  if(!owner.TextSpeed || !owner.ContinueAutoAfterSelection || !owner.ContinueSkipAfterSelection)
    throw UnsupportedCommand("result owner settings are unavailable");
  (void)timing_.ForCharacters(*owner.TextSpeed);(void)timing_.ForApproachResult(0,*owner.TextSpeed);
  if(!owner.Effects.Known)throw UnsupportedCommand("result owner cleanup context is unavailable");
}
void ArchiveApproachResultHost::Act(const ApproachResultRequest& request,const ApproachResultState& result,
    SemanticState& owner,uint64_t now) {
  switch(request.Kind) {
    case ApproachResultAction::PrepareText: {
      const auto catalog=resources_.LoadScriptVoiceEnvelopes(owner.ScriptFileId);
      text_.Prepare(result.Selected,catalog,format_,visual_,font_,metrics_,utility_,nominal_,now);break;
    }
    case ApproachResultAction::DisableVoice:audio_.Disable();break;
    case ApproachResultAction::PrepareVoice:audio_.Prepare(request.Id);break;
    case ApproachResultAction::BeginReveal:text_.Begin(timing_,*owner.TextSpeed,now);break;
    case ApproachResultAction::StartVoice:audio_.Start(now);break;
    case ApproachResultAction::RevealText:text_.RevealAll(now);break;
    case ApproachResultAction::Sound:owner.PendingEvents.push_back({GameEventKind::Sound,request.Id});break;
    case ApproachResultAction::Trophy:owner.PendingEvents.push_back({GameEventKind::Trophy,request.Id});break;
    case ApproachResultAction::Complete:
      CompleteApproachResultPolicy(owner,policy_);owner.PendingCommandIndex=request.Id;break;
  }
}
}
