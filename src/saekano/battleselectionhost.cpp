#include "battleselectionhost.h"
#include <algorithm>
#include <utility>

namespace Impacto::Saekano {
ArchiveBattleSelectionHost::ArchiveBattleSelectionHost(const BattleSelectionEnvironment& environment,
    UiSpriteDefinition sharedBubble,uint32_t holdFrames,const SharedSelectionEnvironment& selection,
    const OrdinaryTextEnvironment& text,const TextFormatEnvironment& format,const GsfFont& font,
    TextLayoutMetrics metrics,uint32_t utilitySize,uint32_t nominalSize,
    const AwbIndex& voiceBank,SceneMessageVoiceOwner& voices,const SemanticState& owner,
    RegisteredReadHistory& history,HistoryEnvironment historyEnvironment)
    :BattleSelectionPresentation(environment,sharedBubble,holdFrames,selection,text,format,font,metrics,utilitySize,nominalSize),
      environment_(environment),voiceBank_(voiceBank),voices_(voices),owner_(owner),history_(history),
      historyEnvironment_(historyEnvironment){}
void ArchiveBattleSelectionHost::RequireFrame()const{
  if(!now_)throw UnsupportedCommand("battle host platform frame is missing");
}
void ArchiveBattleSelectionHost::BeginFrame(uint64_t now,WidgetInputHistory::Snapshot contacts,uint32_t faceMask){
  if(now_ && now<*now_)throw FormatError("battle host clock moved backwards");
  BattleSelectionPresentation::BeginFrame(now,std::move(contacts),faceMask);
  voices_.Output.Update(now);now_=now;
}
ReadHistoryBranch* ArchiveBattleSelectionHost::HistoryRow(int32_t script,int32_t command)const{
  if(script<0 || size_t(script)>=history_.Files.size())
    throw UnsupportedCommand("battle registered history does not contain this script");
  for(auto& row:history_.Files[size_t(script)].Branches)
    if(int32_t(row.Endpoint)==command)return &row; // Native ldrh, not signed.
  return nullptr; // A known table with no matching row is valid native state.
}
void ArchiveBattleSelectionHost::Begin(const ChoiceMenu& menu,int32_t script,int32_t command){
  RequireFrame();
  if(const auto row=HistoryRow(script,command);row && row->Flags.size()!=menu.Entries.size())
    throw FormatError("battle registered history does not match loaded choices");
  ResetBattleSelectionUi(state_,menu,script,command,environment_,*now_);
}
void ArchiveBattleSelectionHost::StopDialogueVoiceNextSlots(){
  StopSceneDialogueNextVoiceSlots(voices_);
}
bool ArchiveBattleSelectionHost::Update(uint64_t now){
  RequireFrame();if(*now_!=now)throw UnsupportedCommand("battle host frame clock does not match command update");
  return UpdateBattleSelectionUi(state_,environment_,BattleSelectionMotionCurve(),*this,now);
}
void ArchiveBattleSelectionHost::MarkHistory(int32_t script,int32_t command,size_t ordinal){
  if(HistorySuppressed())return;
  if(auto* row=HistoryRow(script,command)){
    if(ordinal>=row->Flags.size())throw FormatError("battle history ordinal outside registered row");
    row->Flags[ordinal]=1;
  }
}
void ArchiveBattleSelectionHost::RegisterPreview(int16_t id){
  if(id<0)throw FormatError("negative registered battle preview");
  (void)voiceBank_.Find(uint32_t(id)); // Native801ac650: direct voice.awb ID.
}
void ArchiveBattleSelectionHost::ValidateChannel(uint32_t channel){
  if(channel<11 || channel>12)throw FormatError("battle preview channel outside11..12");
}
void ArchiveBattleSelectionHost::Stop(uint32_t channel){
  ValidateChannel(channel);StopOrdinaryVoiceChannel(state_.Voices,voices_.Output,channel);
}
void ArchiveBattleSelectionHost::Prepare(uint32_t channel,int16_t id){
  ValidateChannel(channel);
  if(std::find(state_.Voices.Catalog.begin(),state_.Voices.Catalog.end(),id)==state_.Voices.Catalog.end())
    throw UnsupportedCommand("battle preview was not registered");
  voices_.Output.Prepare(channel,id);
}
void ArchiveBattleSelectionHost::Start(uint32_t channel){ValidateChannel(channel);voices_.Output.Start(channel);}
bool ArchiveBattleSelectionHost::Playing(uint32_t channel){ValidateChannel(channel);return voices_.Output.Playing(channel);}
void ArchiveBattleSelectionHost::PlayUiSound(uint32_t id){
  sounds_.push_back(id);
}
std::vector<uint32_t> ArchiveBattleSelectionHost::TakeSounds(){return std::exchange(sounds_,{});}
std::vector<BattleSelectionDraw> ArchiveBattleSelectionHost::Draw()const{
  if(!state_.Enabled)return {};
  if(!owner_.ReadColorEnabled)throw UnsupportedCommand("battle history color setting is unknown");
  const auto* row=HistoryRow(state_.Script,state_.Command);
  return BattleSelectionPresentation::Draw(state_,*owner_.ReadColorEnabled,HistorySuppressed(),
      row?std::span<const uint8_t>(row->Flags):std::span<const uint8_t>{});
}
}
