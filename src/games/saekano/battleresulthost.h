#pragma once
#include "battleresult.h"
#include "scenemessage.h"
#include "skipauto.h"

namespace Impacto::Saekano {
// Shares the real dialogue outputs and SDK expression selector. BGM/UI/trophy
// requests enter the same ordered audio/event queue used by ordinary commands.
class ArchiveBattleResultHost final:public BattleResultHost {
 public:
  ArchiveBattleResultHost(GameResources& resources,SceneMessageVoiceOwner& voices,
      Live2dDriver& models,BgmResources& music,SkipAutoState& policy);
  void Validate(const SemanticState& owner)override;
  void Tick(uint64_t now)override{voices_.Output.Update(now);}
  void Act(const BattleResultRequest& request,SemanticState& owner,uint64_t now)override;
 private:
  SceneMessageVoiceOwner& voices_;
  Live2dDriver& models_;
  BgmResources& music_;
  SkipAutoState& policy_;
  std::array<Bytes,8> clips_;
};
}
