#include "approachresult.h"
#include "commands.h"
#include "skipauto.h"

namespace Impacto::Saekano {
void CompleteApproachResultPolicy(SemanticState& owner,SkipAutoState& policy) {
  if(!owner.ContinueAutoAfterSelection || !owner.ContinueSkipAfterSelection)
    throw UnsupportedCommand("result completion policy settings are unavailable");
  if(owner.SkipMode==2 && !*owner.ContinueSkipAfterSelection && !owner.Effects.Known)
    throw UnsupportedCommand("result skip cleanup context is unavailable");
  if(policy.Auto)policy.Auto=*owner.ContinueAutoAfterSelection;
  if(owner.SkipMode==2 && !*owner.ContinueSkipAfterSelection) {
    owner.Effects.CurrentValue=owner.Effects.RestoreValue;
    for(uint32_t i=0;i<2;++i)
      owner.PendingEvents.push_back({GameEventKind::StopChannel,owner.Effects.FirstAudioChannel+int32_t(i)});
    owner.SkipMode=owner.SkipPhase=0;
  }
}
ApproachResultCommand ApproachResultCommand::Decode(std::span<const uint8_t> bytes) {
  PayloadReader reader(bytes);ApproachResultCommand result;
  result.RequiredGauge=reader.I32();
  for(auto& outcome:result.Outcomes) {
    outcome.Destination=reader.I32();outcome.Text=reader.String();outcome.Voice=reader.I32();
    if(outcome.Destination<0 || outcome.Voice < -1)
      throw FormatError("invalid approach result outcome");
  }
  return result; // Original loader ignores command-record alignment bytes.
}
ApproachResultEnvironment ApproachResultEnvironment::Decode(std::span<const uint8_t> bytes) {
  ApproachResultEnvironment result;result.Animation=ModeResultEnvironment::DecodeApproach(bytes);
  const auto prefix=result.Animation.PrefixBytes;
  // Two cursor points (bddc/bde4) precede bdec in the same variable-length loader.
  if(bytes.size()-prefix<20)throw FormatError("truncated approach result fade setting");
  PayloadReader reader(bytes.subspan(prefix+16));result.FadeDuration=reader.I32();
  if(result.FadeDuration<0)throw FormatError("negative approach result fade duration");
  return result;
}
namespace {
void Request(ApproachResultState& s,ApproachResultAction kind,int32_t id=0) {
  s.Requests.push_back({kind,id});
}
void Restart(ApproachResultState& s,size_t index,uint64_t now) {
  auto& t=s.Tweens[index];t.StartUnsigned(t.From,t.To,t.Duration,now);s.Enabled[index]=true;
}
}
void StartApproachResult(ApproachResultState& state,const ApproachResultCommand& command,
                         const ApproachResultEnvironment& environment,int32_t gauge,uint64_t now) {
  const auto& e=environment.Animation;ApproachResultState s;
  if(environment.FadeDuration<0)throw FormatError("negative approach result fade duration");
  s.Started=s.Visible=true;s.Outcome=gauge>=command.RequiredGauge?0:1;s.Selected=command.Outcomes[s.Outcome];
  s.Tweens[0].Start(0,100,e.Progress,now);
  s.Tweens[1].Start(e.HorizontalStart,0,e.HorizontalSlide,now);
  s.Tweens[2].Start(0,255,e.HorizontalOpacity,now);
  s.Tweens[3].Start(e.VerticalStart,0,e.VerticalSlide,now);
  s.Tweens[4].Start(0,255,e.VerticalOpacity,now);
  s.Tweens[5].Start(0,100,e.SecondaryProgress,now);
  s.Tweens[6].Start(0,255,e.TextOpacity,now);s.Enabled[0]=true;
  Request(s,ApproachResultAction::PrepareText);
  Request(s,ApproachResultAction::DisableVoice,11);
  if(s.Selected.Voice!=-1)Request(s,ApproachResultAction::PrepareVoice,s.Selected.Voice);
  state=std::move(s);
}
bool UpdateApproachResult(ApproachResultState& s,const ApproachResultEnvironment& e,
                          const ApproachResultInput& input,uint64_t now) {
  if(!s.Started)throw UnsupportedCommand("approach result Start was suppressed");
  if(s.Completed)return true;
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  switch(s.Phase) {
    case 0:
      s.Tweens[0].Tick(now);
      if(s.Tweens[0].Finished()) {
        for(size_t i=1;i<=4;++i)Restart(s,i,now);
        s.PendingPhase=1;
      }
      break;
    case 1:
      for(size_t i=1;i<=4;++i)s.Tweens[i].Tick(now);
      if(s.Tweens[2].Finished() && s.Tweens[4].Finished()) {
        Restart(s,5,now);s.PendingPhase=2;
      }
      break;
    case 2:
      s.Tweens[5].Tick(now);
      if(s.Tweens[5].Finished()) {
        Request(s,ApproachResultAction::BeginReveal,s.Selected.Voice);
        Request(s,ApproachResultAction::StartVoice,11);s.PendingPhase=3;
      }
      break;
    case 3:
      if(input.TextRevealed) {
        Restart(s,6,now);Request(s,ApproachResultAction::Sound,s.Outcome==0?12:13);s.PendingPhase=4;
      } else if(input.Accept || input.Released)Request(s,ApproachResultAction::RevealText);
      break;
    case 4:
      s.Tweens[6].Tick(now);
      if(s.Tweens[6].Finished()) {
        Request(s,ApproachResultAction::Trophy,s.Outcome==0?35:36);s.Cursor=true;s.PendingPhase=5;
      }
      break;
    case 5:
      if(input.Accept || input.Released) {
        Request(s,ApproachResultAction::Sound,3);Request(s,ApproachResultAction::DisableVoice,11);
        s.Fade.Start(0,255,e.FadeDuration,now);s.FadeActive=true;s.PendingPhase=6;
      }
      break;
    case 6:
      s.Fade.Tick(now);s.FadeOpacity=s.Fade.Current;
      if(s.Fade.Finished())s.PendingPhase=7;
      break;
    case 7:
      Request(s,ApproachResultAction::Complete,s.Selected.Destination);s.Completed=true;return true;
    default:throw FormatError("invalid approach result phase");
  }
  return false;
}
}
