#include "battleselection.h"
#include "commands.h"
#include <bit>

namespace Impacto::Saekano {
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
