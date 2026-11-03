#include "systemowner.h"
#include "commands.h"
#include "skipauto.h"
#include <memory>

namespace Impacto::Saekano {
namespace {
MusicHistoryState Music(const SystemSaveHistory& source) {
  MusicHistoryState result;result.ExtentKnown=true;result.Default=source.Fallback;
  for(const auto value:source.Values)result.Entries.push_back(value);
  return result;
}
SystemSaveHistory SavedMusic(const MusicHistoryState& source) {
  if(!source.AllKnown())throw UnsupportedCommand("system snapshot contains unknown music/battle history");
  SystemSaveHistory result;result.Fallback=source.Default;
  for(size_t i=0;i<source.Entries.size();++i)result.Values.push_back(ExactMusicStatus(source,i));
  return result;
}
void Bool(uint8_t& raw,bool value) {
  // Preserve noncanonical native bytes when their interpreted value is unchanged.
  if((raw!=0)!=value)raw=uint8_t(value);
}
uint8_t Master(std::optional<int32_t> value) {
  if(!value || *value<0 || *value>2550 || *value%10)
    throw UnsupportedCommand("system snapshot audio master cannot be represented as an option byte");
  return uint8_t(*value/10);
}
}
NativeSystemSave MakeFreshSystemProgress(Bytes firstDat,bool initiallyRead) {
  NativeSystemSave result;
  result.Bytes6022c={2,2,0,10,1,1,1,8,8,8,10,5,0};
  result.Bytes6023a.fill(1);
  result.Histories[8].Values.resize(44,0);
  for(const size_t id:{0u,16u,32u,43u})result.Histories[8].Values[id]=1;
  StoreSystemReadHistory(result,RegisteredReadHistory::LoadFirst(std::move(firstDat),initiallyRead));
  (void)result.Encode();return result;
}
void LoadSystemOwner(SemanticState& owner,const NativeSystemSave& progress,
                     std::vector<int32_t> requiredCg) {
  // Validate the complete input before publishing any owner change.
  (void)progress.Encode();(void)RestoreSystemReadHistory(progress);
  for(const auto id:requiredCg)if(id<0 || id>1000000)throw FormatError("invalid saved CG requirement");
  auto next=std::make_unique<SemanticState>(owner);
  next->SystemProgress=progress;next->Bank1=progress.Bank1;
  next->CgHistory={true,progress.Histories[0].Values,std::move(requiredCg),progress.Histories[0].Fallback,{}};
  next->MusicHistory=Music(progress.Histories[1]);next->BattleHistory=Music(progress.Histories[6]);
  ApplySystemPreferences(*next,progress);
  owner=std::move(*next);
}
void ApplySystemPreferences(SemanticState& owner,const NativeSystemSave& progress){
  const auto& option=progress.Bytes6022c;
  owner.TextSpeed=option[0];owner.AutoSpeed=option[1];owner.SkipUnreadEnabled=option[2];owner.WindowOpacity=option[3];
  owner.ContinueAutoAfterSelection=option[4]!=0;owner.ContinueSkipAfterSelection=option[5]!=0;owner.ReadColorEnabled=option[6]!=0;
  owner.BgmMasterVolume=int32_t(option[7])*10;owner.SeMasterVolume=int32_t(option[8])*10;
  owner.VoiceSettings=OrdinaryVoiceSettings{option[10],option[12]!=0,progress.Bytes6023a};
}
NativeSystemSave SnapshotSystemOwner(const SemanticState& owner,
                                     const RegisteredReadHistory& history) {
  if(!owner.SystemProgress)throw UnsupportedCommand("complete persistent system owner is unavailable");
  if(!owner.CgHistory.Known || owner.CgHistory.Partial)
    throw UnsupportedCommand("system snapshot contains unknown CG history");
  if(!owner.TextSpeed || !owner.AutoSpeed || !owner.WindowOpacity || !owner.ContinueAutoAfterSelection ||
     !owner.ContinueSkipAfterSelection || !owner.ReadColorEnabled || !owner.VoiceSettings)
    throw UnsupportedCommand("system snapshot preferences are incomplete");
  if(owner.SkipUnreadEnabled<0 || owner.SkipUnreadEnabled>255)throw FormatError("invalid saved skip preference");
  auto result=*owner.SystemProgress;result.Bank1=owner.Bank1;
  result.Histories[0]={owner.CgHistory.Values,owner.CgHistory.Default};
  result.Histories[1]=SavedMusic(owner.MusicHistory);result.Histories[6]=SavedMusic(owner.BattleHistory);
  auto& option=result.Bytes6022c;
  option[0]=*owner.TextSpeed;option[1]=*owner.AutoSpeed;option[2]=uint8_t(owner.SkipUnreadEnabled);option[3]=*owner.WindowOpacity;
  Bool(option[4],*owner.ContinueAutoAfterSelection);Bool(option[5],*owner.ContinueSkipAfterSelection);Bool(option[6],*owner.ReadColorEnabled);
  option[7]=Master(owner.BgmMasterVolume);option[8]=Master(owner.SeMasterVolume);
  option[10]=owner.VoiceSettings->Volume;Bool(option[12],owner.VoiceSettings->StopOnAdvance);
  result.Bytes6023a=owner.VoiceSettings->Enabled;StoreSystemReadHistory(result,history);
  (void)result.Encode();return result;
}
}
