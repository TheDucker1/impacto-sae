#include "battleselection.h"
#include "commands.h"
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
float EaseCard(float input,const std::array<float,4>& curve) {
  // Shared native9ebbc: float bisection, tolerance0.01. Preserve the VFP
  // operation order and allow Y outside0..1 for the authored card overshoot.
  const float lower=input-0.01f,upper=input+0.01f;
  float lo=0,hi=1;
  for(size_t i=0;i<64;++i) {
    const float midpoint=(lo+hi)*0.5f,inverse=1.0f-midpoint;
    const float three=inverse*3.0f,square=midpoint*midpoint;
    const float a=(three*midpoint)*midpoint,b=(three*inverse)*midpoint,cube=square*midpoint;
    float x=a*curve[2];x=x+b*curve[0];x=x+cube;
    if(x<lower){lo=midpoint;continue;}
    if(x>upper){hi=midpoint;continue;}
    float y=a*curve[3];y=y+b*curve[1];return y+cube;
  }
  throw FormatError("battle card ease cannot converge");
}
}
void BattleCardMotion::Configure(float from,float to,int32_t duration,std::array<float,4> curve,bool enabled,uint64_t now) {
  if(!std::isfinite(from) || !std::isfinite(to) || duration<0)
    throw FormatError("invalid battle card motion");
  for(float value:curve)if(!std::isfinite(value))throw FormatError("invalid battle card curve");
  Current=From=from;To=to;Curve=curve;Duration=duration;Epoch=now;Elapsed=0;Enabled=enabled;
}
void BattleCardMotion::Restart(uint64_t now) { Epoch=now;Elapsed=0;Enabled=true;Current=From; }
void BattleCardMotion::EnableIfDormant(uint64_t now) { if(!Enabled)Restart(now); }
void BattleCardMotion::Tick(uint64_t now) {
  if(!Enabled)return;
  Elapsed=std::bit_cast<int32_t>(uint32_t(now)-uint32_t(Epoch));
  if(Elapsed>=Duration){Current=To;return;}
  if(Elapsed<0)throw FormatError("battle card clock moved backward");
  const float ratio=float(Elapsed)/float(Duration),curve=EaseCard(ratio,Curve);
  const float delta=To-From,product=delta*curve;
  Current=From+product;
}
void StartBattleSelection(SemanticState& owner,const ChoiceMenu& menu,
    const BattleSelectionEnvironment& environment,BattleSelectionHost& host,uint64_t now) {
  if(menu.DurationMs>INT32_MAX || environment.RevealDuration<0)
    throw UnsupportedCommand("negative native battle selection duration is not bound");
  if(menu.Entries.empty() || menu.Entries.size()>4)throw FormatError("battle selection entry count outside1..4");
  auto& s=owner.BattleSelection;const bool active=s.Active;s={};s.Active=active;
  s.Script=owner.ScriptFileId;s.Command=owner.CurrentCommandIndex;
  for(const auto& entry:menu.Entries)s.Destinations.push_back(entry.Destination);
  host.Begin(menu,s.Script,s.Command);
  s.InitialSeconds=int32_t(menu.DurationMs)/1000;s.Seconds=s.InitialSeconds;
  s.Countdown.StartUnsigned(0,s.InitialSeconds,menu.DurationMs,now);
  s.Reveal.Start(0,100,environment.RevealDuration,now);
  host.StopDialogueVoiceNextSlots();
  owner.BattleEntry.SelectStartedCount=std::bit_cast<int32_t>(uint32_t(owner.BattleEntry.SelectStartedCount)+1);
  s.Started=true;
}
bool UpdateBattleSelection(SemanticState& owner,BattleSelectionHost& host,uint64_t now) {
  auto& s=owner.BattleSelection;
  if(!s.Started)throw UnsupportedCommand("battle selection command runtime is missing");
  if(s.Completed)return true;
  s.Reveal.Tick(now);
  if(!s.Reveal.Finished())return false;
  if(!s.Active){host.Enable();s.Active=true;}
  if(!s.CountdownEnabled){s.Countdown.StartUnsigned(0,s.InitialSeconds,s.Countdown.Duration,now);s.CountdownEnabled=true;}
  if(!s.Paused)s.Countdown.Tick(now);
  s.Seconds=std::bit_cast<int32_t>(uint32_t(s.InitialSeconds)-uint32_t(s.Countdown.Current));
  const auto commit=[&](bool history) {
    const auto ordinal=host.Ordinal();
    if(ordinal>=s.Destinations.size())throw FormatError("battle UI ordinal outside loaded entries");
    owner.PendingCommandIndex=s.Destinations[ordinal];s.Active=false;
    if(history && !host.HistorySuppressed())host.MarkHistory(s.Script,s.Command,ordinal);
    s.Completed=true;
  };
  if(s.Countdown.Finished()){host.Timeout();commit(false);}
  else {
    if(!host.Update(now))commit(true);
    if(host.Phase()>=6)s.Paused=true;
  }
  return s.Completed;
}
}
