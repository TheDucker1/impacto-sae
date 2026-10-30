#include "battleselection.h"
#include "commands.h"
#include "music.h"
#include <algorithm>
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
void ResetBattleSelectionUi(BattleSelectionUiState& s,const ChoiceMenu& menu,int32_t script,int32_t command,
    const BattleSelectionEnvironment& e,uint64_t now) {
  if(menu.Entries.empty() || menu.Entries.size()>4)throw FormatError("battle UI row count outside1..4");
  s.Enabled=false;s.Ordinal=0;s.Script=script;s.Command=command;s.Rows.clear();
  for(const auto& row:menu.Entries) {
    // Original copies an uninitialized word for missing extras. Initialize the
    // unused timeout presentation safely; decimal parsing matches strtol32.
    const auto presentation=row.ExtraStrings.empty()?0:ParseBgmTrack(row.ExtraStrings.front());
    s.Rows.push_back({row.Destination,presentation,row.PreviewDescriptor,row.Label});
  }
  for(size_t i=0;i<3;++i){s.Fades[i].Start(255,0,e.FadeDuration,now);s.FadeEnabled[i]=false;}
  // Native reset deliberately retains current/pending phase, float motions,
  // widget state, selected voice slot and the descriptor/prepared queue.
  s.Known=true;
}
void EnableBattleSelectionUi(BattleSelectionUiState& s) { s.Enabled=true;s.PendingPhase=0; }
void TimeoutBattleSelectionUi(BattleSelectionUiState& s) {
  // Original8b04a changes only phase; command7ea6a separately disables UI+8.
  s.Phase=8;s.PendingPhase.reset();
}
bool UpdateBattleSelectionUi(BattleSelectionUiState& s,const BattleSelectionEnvironment& e,
    std::array<float,4> curve,BattleSelectionUiServices& output,uint64_t now) {
  if(!s.Known)throw UnsupportedCommand("battle selection UI owning context is missing");
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  const auto restart=[&](size_t i){auto& t=s.Fades.at(i);t.StartUnsigned(t.From,t.To,t.Duration,now);s.FadeEnabled[i]=true;};
  const auto tick=[&](size_t i){if(s.FadeEnabled[i])s.Fades[i].Tick(now);};
  if(s.Phase==0) {
    s.Voices.BaseChannel=11;
    for(size_t i=0;i<s.Rows.size();++i) {
      const auto& row=s.Rows[i];output.PrepareRow(i,row,e.TextWidth); // global80261758.
      if(row.Preview!=-1) {
        output.RegisterPreview(row.Preview);
        if(std::find(s.Voices.Catalog.begin(),s.Voices.Catalog.end(),row.Preview)==s.Voices.Catalog.end())
          s.Voices.Catalog.push_back(row.Preview);
      }
    }
    constexpr std::array<std::array<uint32_t,2>,3> textures{{{4,6},{1,3},{7,9}}};
    for(size_t i=1;i<s.Rows.size();++i) {
      const auto control=i-1;output.ClearControl(control);const auto type=s.Rows[i].Presentation;
      if(type<0 || type>2)continue; // Native leaves this control without states.
      output.AddControlState(control,textures[type][0],e.Positions.at(control),e.CardScale,e.FillColors.at(control),true);
      output.AddControlState(control,textures[type][1],e.Positions.at(control),e.CardScale,e.FrameColors.at(control),false);
      for(size_t j=0;j<2;++j)output.SetControlPulse(control,j,e.Pulses[size_t(type)]);
    }
    s.Phase=1;
  }
  if(s.Phase==1) {
    for(size_t i=0;i<3;++i) {
      s.Motions[i].Configure(float(e.Travel),0,e.MotionDuration,curve,false,now);
      s.Fades[i].Start(0,255,e.MotionDuration,now);s.FadeEnabled[i]=false;
    }
    s.Motions[0].Restart(now);restart(0);s.Phase=2;
  }
  if(s.Phase==2) {
    bool done=true;
    for(size_t i=0;i<3;++i) {
      s.Motions[i].Tick(now);tick(i);
      if(i<2 && s.Motions[i].Current<=float(e.Travel/2)) {
        s.Motions[i+1].EnableIfDormant(now);if(!s.FadeEnabled[i+1])restart(i+1);
      }
      done=(s.Motions[i].Current==s.Motions[i].To)&&done;
    }
    for(size_t i=1;i<s.Rows.size();++i) {
      const float position=s.Motions[i-1].Current;
      if(!std::isfinite(position) || double(position)<INT32_MIN || double(position)>INT32_MAX)
        throw FormatError("battle card offset outside integer range");
      s.Offsets[i-1]=int32_t(position);output.SetControlOffset(i-1,s.Offsets[i-1]);
    }
    if(done) {
      s.Phase=3;
      for(size_t i=0;i<3;++i){s.Fades[i].Start(255,0,e.FadeDuration,now);s.FadeEnabled[i]=false;}
    }
  }
  if(s.Phase==3){s.Ordinal=0;s.Phase=4;}
  if(s.Phase==4) {
    for(size_t i=0;i<3;++i)if(output.FacePressed(i)||output.PointerPressed(i)) {
      if(i+1>=s.Rows.size())throw FormatError("battle control has no loaded row");
      output.PlayUiSound(3);s.Ordinal=i+1;s.Phase=6;break;
    }
    for(size_t i=1;i<s.Rows.size();++i){s.Offsets[i-1]=0;output.SetControlOffset(i-1,0);}
  }
  if(s.Phase==6) {
    for(size_t i=0;i<3;++i)if(i+1!=s.Ordinal)restart(i);
    if(!output.PreviewEnabled())s.Phase=8;
    else {
      s.SelectedVoiceSlot=0;const auto preview=s.Rows.at(s.Ordinal).Preview;
      if(preview!=-1)s.SelectedVoiceSlot=PrepareOrdinaryVoice(s.Voices,preview,false,output);
      s.Phase=7;
    }
  }
  if(s.Phase==7) {
    bool done=true;
    for(size_t i=0;i<3;++i){tick(i);if(i+1!=s.Ordinal)done=s.Fades[i].Finished()&&done;}
    if(!output.Playing(11+s.SelectedVoiceSlot) && done)s.Phase=8;
  }
  if(s.Phase==8) {
    if(s.Ordinal==0 || s.Ordinal>3)throw UnsupportedCommand("battle timeout dismissal has no interactive ordinal");
    restart(s.Ordinal-1);s.Phase=9;
  }
  if(s.Phase==9) { const auto selected=s.Ordinal-1;tick(selected);if(s.Fades.at(selected).Finished())s.Phase=10; }
  if(s.Phase==10){s.Enabled=false;return false;}
  return true;
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
