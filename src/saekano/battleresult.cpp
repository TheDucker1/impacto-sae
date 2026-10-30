#include "battleresult.h"
#include "commands.h"
#include <algorithm>
#include <bit>

namespace Impacto::Saekano {
BattleResultCommand BattleResultCommand::Decode(std::span<const uint8_t> bytes){
  PayloadReader reader(bytes);BattleResultCommand out;
  for(auto& destination:out.Destinations){destination=reader.I32();if(destination<0)throw FormatError("negative battle result destination");}
  return out;
}
namespace {
void Request(BattleResultState& s,BattleResultAction kind,int32_t id=0,int32_t parameter=0){s.Requests.push_back({kind,id,parameter});}
void Tick(ScriptLinearTween& t,uint64_t now){
  const auto delta=std::bit_cast<int32_t>(uint32_t(now-t.Sample));
  if(delta<0)t.Epoch=t.Sample=now;
  t.Tick(t.Sample+uint32_t(delta<0?0:delta));
}
void ValidatePolicy(const SemanticState& owner){
  if(!owner.BattleEntry.SavedPolicy || !owner.ContinueAutoAfterSelection || !owner.ContinueSkipAfterSelection)
    throw UnsupportedCommand("battle result saved/owner policy is unavailable");
  if(owner.BattleEntry.SavedPolicy->Mode==2 && !*owner.ContinueSkipAfterSelection && !owner.Effects.Known)
    throw UnsupportedCommand("battle result primary voice cleanup state is unavailable");
}
void RestorePolicy(SemanticState& owner){
  ValidatePolicy(owner);const auto& saved=*owner.BattleEntry.SavedPolicy;
  owner.BattleEntry.MessageModeByte=saved.MessageModeByte?uint8_t(*owner.ContinueAutoAfterSelection):0;
  if(saved.Mode==2){
    if(!*owner.ContinueSkipAfterSelection){
      owner.Effects.CurrentValue=owner.Effects.RestoreValue;
      Request(owner.BattleResult,BattleResultAction::StopPrimaryVoices);
      owner.SkipMode=owner.SkipPhase=0;
    }else{owner.SkipMode=2;owner.SkipPhase=saved.Phase;}
  }
}
void BeginItem(BattleResultState& s,size_t item,uint64_t now){
  s.Items[item].Restart(now);
  for(auto* t:{&s.Alpha[item],&s.Travel[item]})t->StartUnsigned(t->From,t->To,t->Duration,now);
  s.AlphaEnabled[item]=s.TravelEnabled[item]=true;
}
}
void StartBattleResult(SemanticState& owner,const BattleResultCommand& command,
    const BattleResultEnvironment& e,uint64_t now){
  ValidatePolicy(owner);
  if(!owner.Battle.Known || !owner.Live2d.Known || !owner.BattleResult.Known || !owner.MusicHistory.ExtentKnown)
    throw UnsupportedCommand("battle result owning presentation/actor/music state is unavailable");
  if(e.ItemDuration<0 || e.ItemDuration>INT32_MAX/2 || e.FadeDuration<0 || e.RevealDuration<0)
    throw FormatError("invalid battle result timing");
  BattleResultState s;s.Known=s.Started=true;s.Visible=owner.BattleResult.Visible;s.Command=command;
  s.Outcome=owner.BattleGauge<50?1:owner.BattleGauge>50?0:2;
  for(size_t i=0;i<4;++i){
    s.Items[i].Configure(0,e.ItemAmplitude,e.ItemDuration,false,now);
    s.Travel[i].Start(0,e.ItemAmplitude,2*e.ItemDuration,now);
    s.Alpha[i].Start(0,255,2*e.ItemDuration,now);
  }
  s.Reveal.Start(0,100,e.RevealDuration,now);
  Request(s,BattleResultAction::StopDialogueVoices,1);
  owner.BattleEntry.SelectStartedCount=std::bit_cast<int32_t>(uint32_t(owner.BattleEntry.SelectStartedCount)+1);
  owner.BattleResult=std::move(s);
}
bool UpdateBattleResult(SemanticState& owner,const BattleResultEnvironment& e,
    bool accept,bool auxiliaryPressed,uint64_t now){
  auto& s=owner.BattleResult;
  if(!s.Started)throw UnsupportedCommand("battle result Start was suppressed");
  if(s.Completed)return true;
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  const auto fade=[&](int32_t from,int32_t to){s.Fade.Start(from,to,e.FadeDuration,now);s.FadeEnabled=true;};
  const auto tickFade=[&](){Tick(s.Fade,now);owner.ScreenFade=s.Fade.Current;return s.Fade.Finished();};
  if(s.Phase==0){fade(0,255);s.Phase=1;}
  if(s.Phase==1 && tickFade()){
    MarkMusicPlayed(owner.MusicHistory,7);
    Request(s,BattleResultAction::BeginResultMusic,7); // Native Stop0 then Play7; retain BGM volume.
    for(size_t side=0;side<2;++side){
      const auto logical=owner.Battle.SideCharacters[side];
      const auto actor=std::find_if(owner.Live2d.Records.begin(),owner.Live2d.Records.end(),[&](const auto& r){return r.LogicalId==logical;});
      // Native has no attached-resource check here; avoid dereferencing a null
      // renderer while retaining independent handling of the other side.
      if(actor==owner.Live2d.Records.end() || !actor->ResourceId)continue;
      const auto pose=s.Outcome==2?e.EqualPose:((side==0)==(s.Outcome==0)?e.AbovePose:e.BelowPose);
      Request(s,BattleResultAction::SetPose,logical,pose);
    }
    s.PendingPhase=2;
  }
  if(s.Phase==2){s.Visible=true;s.PendingPhase=3;}
  if(s.Phase==3){fade(255,0);s.Phase=4;}
  if(s.Phase==4 && tickFade())s.PendingPhase=5;
  if(s.Phase==5){s.Reveal.StartUnsigned(s.Reveal.From,s.Reveal.To,s.Reveal.Duration,now);s.RevealEnabled=true;s.Phase=6;}
  if(s.Phase==6){
    Tick(s.Reveal,now);
    if(s.Reveal.Finished()){
      BeginItem(s,0,now);
      Request(s,BattleResultAction::Sound,s.Outcome==2?0x13:0x12);
      const auto descriptor=s.Outcome==0?owner.Battle.SideCharacters[0]:owner.Battle.SideCharacters[1]+(s.Outcome==2?4:0);
      Request(s,BattleResultAction::PlayVoice,descriptor);s.PendingPhase=7;
    }
  }
  if(s.Phase==7){
    if(s.Item>=4)throw FormatError("battle result item exceeded active animation");
    s.Items[s.Item].Tick(now);Tick(s.Alpha[s.Item],now);Tick(s.Travel[s.Item],now);
    if(s.Items[s.Item].Finished()){
      ++s.Item;
      if(s.Item<4)BeginItem(s,s.Item,now);
      else{if(s.Outcome==2)Request(s,BattleResultAction::Trophy,38);s.PendingPhase=8;}
    }
  }
  if(s.Phase==8 && (accept||auxiliaryPressed)){Request(s,BattleResultAction::StopVoice,7);s.PendingPhase=9;}
  if(s.Phase==9){fade(0,255);s.PendingPhase=10;}
  if(s.Phase==10 && tickFade())s.PendingPhase=11;
  if(s.Phase==11){
    Request(s,BattleResultAction::StopResultMusic);s.Visible=false;
    owner.PendingCommandIndex=s.Command.Destinations.at(s.Outcome);
    RestorePolicy(owner);s.Completed=true;Request(s,BattleResultAction::Complete);
  }
  return s.Completed;
}
}
