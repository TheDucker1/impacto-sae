#include "battleresulthost.h"
#include <algorithm>

namespace Impacto::Saekano {
ArchiveBattleResultHost::ArchiveBattleResultHost(GameResources& resources,SceneMessageVoiceOwner& voices,
    Live2dDriver& models,BgmResources& music,SkipAutoState& policy)
    :voices_(voices),models_(models),music_(music),policy_(policy){
  for(size_t i=0;i<clips_.size();++i){
    auto clip=resources.LoadBattleResultVoice(i);(void)HcaWaveReader(clip.Data);clips_[i]=std::move(clip.Data);
  }
}
void ArchiveBattleResultHost::Validate(const SemanticState& owner){
  if(!voices_.SecondaryQueue || voices_.Queue.BaseChannel!=7 || voices_.SecondaryQueue->BaseChannel!=9)
    throw UnsupportedCommand("result dialogue owners are not bound to7..10");
  if(!owner.BattleEntry.SavedPolicy || !owner.ContinueAutoAfterSelection || !owner.ContinueSkipAfterSelection)
    throw UnsupportedCommand("result saved/owner auto-skip policy is unavailable");
  if(owner.BattleEntry.SavedPolicy->Mode==2 && !*owner.ContinueSkipAfterSelection &&
     (!owner.Effects.Known || owner.Effects.FirstAudioChannel!=int32_t(voices_.Queue.BaseChannel)))
    throw UnsupportedCommand("result primary voice cleanup owner is unavailable");
  for(const auto logical:owner.Battle.SideCharacters){
    const auto actor=std::find_if(owner.Live2d.Records.begin(),owner.Live2d.Records.end(),[&](const auto& r){return r.LogicalId==logical;});
    if(actor!=owner.Live2d.Records.end() && actor->ResourceId &&
       (!models_.Supports(6) || !models_.ResourceReady(*actor->ResourceId)))
      throw UnsupportedCommand("result expression renderer is unavailable");
  }
  const auto descriptor=owner.BattleGauge>50?owner.Battle.SideCharacters[0]:
      owner.Battle.SideCharacters[1]+(owner.BattleGauge==50?4:0);
  if(descriptor<0 || size_t(descriptor)>=clips_.size())throw FormatError("result voice descriptor outside eight original clips");
  music_.Prepare(7);
}
void ArchiveBattleResultHost::Act(const BattleResultRequest& r,SemanticState& owner,uint64_t now){
  switch(r.Kind){
    case BattleResultAction::StopDialogueVoices:
      if(r.Id!=1)throw FormatError("result Start requires native immediate-stop flag1");
      StopSceneDialogueVoicesImmediately(voices_);break;
    case BattleResultAction::BeginResultMusic:
      if(r.Id!=7)throw FormatError("unexpected result music track");
      owner.PendingEvents.push_back({GameEventKind::StopBgmWithCurrentFade,0});
      owner.PendingEvents.push_back({GameEventKind::PlayBgmRetainingGain,r.Id});break;
    case BattleResultAction::SetPose:{
      const auto actor=std::find_if(owner.Live2d.Records.begin(),owner.Live2d.Records.end(),[&](const auto& a){return a.LogicalId==r.Id;});
      if(actor==owner.Live2d.Records.end() || !actor->ResourceId)break;
      Live2dCommand command;command.Selector=6;command.Target=r.Id;command.Value=r.Parameter;
      models_.Apply(*actor,command,now);break; // Deliberately leaves script-record Pose unchanged.
    }
    case BattleResultAction::Sound:owner.PendingEvents.push_back({GameEventKind::Sound,r.Id});break;
    case BattleResultAction::Trophy:owner.PendingEvents.push_back({GameEventKind::Trophy,r.Id});break;
    case BattleResultAction::PlayVoice:
      if(r.Id<0 || size_t(r.Id)>=clips_.size())throw FormatError("result voice descriptor outside original array");
      PrepareSceneMemoryVoice(voices_,7,clips_[size_t(r.Id)]);voices_.Output.Start(7);break;
    case BattleResultAction::StopVoice:StopSceneDialogueVoice(voices_,uint32_t(r.Id));break;
    case BattleResultAction::StopResultMusic:owner.PendingEvents.push_back({GameEventKind::StopBgmWithCurrentFade,0});break;
    case BattleResultAction::StopPrimaryVoices:
      StopSceneDialogueVoice(voices_,voices_.Queue.BaseChannel);StopSceneDialogueVoice(voices_,voices_.Queue.BaseChannel+1);break;
    case BattleResultAction::Complete:policy_.Auto=owner.BattleEntry.MessageModeByte!=0;break;
  }
}
}
