#include "battleentry.h"
#include "commands.h"

namespace Impacto::Saekano {
BattleEntryCommand BattleEntryCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);BattleEntryCommand out;
  out.History=reader.I32();out.Groups[0]=reader.I32();out.Entries[0]=reader.I32();
  out.Groups[1]=reader.I32();out.Entries[1]=reader.I32();out.Gauge=reader.I32();return out;
}
void InitializeFreshBattleOwner(SemanticState& state) {
  if(state.BattleHistory.ExtentKnown || !state.BattleHistory.Entries.empty() || state.BattleHistory.Default)
    throw FormatError("fresh battle history would overwrite existing owner state");
  state.BattleHistory={true,{},0};
}
void LoadBattleEntryFields(SemanticState& state,const BattleEntryCommand& command) {
  if(!state.BattleEntry.Known || !state.MouthControl || !state.Battle.Known)
    throw UnsupportedCommand("initial battle context is unavailable");
  state.BattleEntry.Enabled=true;state.BattleEntry.SelectLoadedCount=1;state.BattleEntry.SelectStartedCount=0;
  state.Battle.SideCharacters=command.Groups;state.BattleGauge=command.Gauge;state.DayEnabled=0;
  state.MouthControl->DualCleanup=true;state.MouthControl->CleanupIds=command.Groups;
}
void StartBattleEntry(SemanticState& state,const BattleEntryCommand& command,BattleResources& resources) {
  if(!state.BattleEntry.Known || !state.Battle.Known || !state.MouthControl || !state.BattleHistory.ExtentKnown)
    throw UnsupportedCommand("initial battle context/history is unavailable");
  if(command.History<0 || command.Groups[0]<0 || command.Groups[1]<0 || command.Entries[0]<0 || command.Entries[1]<0)
    throw FormatError("negative battle history/resource index");
  auto history=state.BattleHistory;MarkMusicPlayed(history,size_t(command.History));
  resources.Begin(command);
  state.BattleEntry.SavedPolicy=BattleEntryState::Policy{state.BattleEntry.MessageModeByte,state.SkipPhase,state.SkipMode};
  state.BattleEntry.MessageModeByte=1;state.SkipPhase=0;state.SkipMode=0;
  state.BattleEntry.Enabled=true;state.BattleEntry.Request=command;
  state.Battle.SideCharacters=command.Groups;state.Battle.Standings={1,1};state.BattleGauge=command.Gauge;
  state.MouthControl->DualCleanup=true;state.MouthControl->CleanupIds=command.Groups;state.DayEnabled=0;
  state.BattleHistory=std::move(history);
  state.PendingEvents.push_back({GameEventKind::StopSe,2});state.PendingEvents.push_back({GameEventKind::StopSe,3});
}
bool UpdateBattleEntry(SemanticState& state,const BattleResources& resources,bool skipping) {
  const bool done=resources.Status().Idle();
  if(skipping)state.SkipPhase=done?1:2;
  return done;
}
}
