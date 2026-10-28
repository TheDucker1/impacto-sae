#include "approachmission.h"
#include "commands.h"

namespace Impacto::Saekano {
namespace {
void Restart(ApproachMissionState& s,size_t i,uint64_t now) {
  auto& t=s.Tweens[i];t.StartUnsigned(t.From,t.To,t.Duration,now);s.Enabled[i]=true;
}
void Finish(ApproachMissionState& s,size_t i) {
  auto& t=s.Tweens[i];t.Current=t.To;t.Sample=t.Epoch+t.Duration;
}
}
void StartApproachMission(ApproachMissionState& s,const ApproachMissionEnvironment& e,
                          int32_t mission,uint64_t now) {
  if(mission < -1 || e.LeftWidth<0 || e.EntranceAlpha<0 || e.LeftSlide<0 ||
      e.RightSlide<0 || e.Hold<0 || e.Exit<0)
    throw FormatError("invalid approach mission start input");
  ApproachMissionState n;n.Started=n.Visible=true;n.Mission=mission;
  n.Tweens[0].Start(0,255,e.EntranceAlpha,now);
  n.Tweens[1].Start(-e.LeftWidth,e.LeftX,e.LeftSlide,now);
  n.Tweens[2].Start(960,e.RightX,e.RightSlide,now);
  n.Tweens[3].Start(0,255,e.Hold,now);
  n.Tweens[4].Start(0,e.ExitDistance,e.Exit,now);
  n.Tweens[5].Start(255,0,e.Exit,now);
  n.Enabled={true,true,true,false,false,false};
  if(mission!=-1)n.Requests.push_back({ApproachMissionAction::PlayVoice,mission});
  s=std::move(n);
}
bool UpdateApproachMission(ApproachMissionState& s,const ApproachMissionEnvironment& e,
                           bool accept,uint64_t now) {
  if(!s.Started)throw UnsupportedCommand("approach mission Start was suppressed");
  if(s.PendingStage){s.Stage=*s.PendingStage;s.PendingStage.reset();}
  if(s.Stage==0) {
    for(size_t i=0;i<3;++i)s.Tweens[i].Tick(now);
    if(s.Tweens[0].Finished() && s.Tweens[1].Finished() && s.Tweens[2].Finished()) {
      Restart(s,3,now);s.PendingStage=1;
    }
    if(accept){for(size_t i=0;i<4;++i)Finish(s,i);s.PendingStage=2;}
  }
  if(s.Stage==1) {
    s.Tweens[3].Tick(now);
    if(s.Tweens[3].Finished())s.PendingStage=2;
  }
  if(s.Stage==2 && accept) {
    s.Requests.push_back({ApproachMissionAction::ConfirmSound,3});
    Restart(s,4,now);Restart(s,5,now);
    s.Tweens[0].Start(255,0,e.Exit,now);
    s.Tweens[3].Start(255,0,e.Exit,now);s.Enabled[3]=true;
    s.PendingStage=3;
  }
  if(s.Stage==3) {
    for(const auto i:{size_t(4),size_t(5),size_t(0),size_t(3)})s.Tweens[i].Tick(now);
    if(s.Tweens[4].Finished())s.PendingStage=4;
    if(accept){for(const auto i:{size_t(4),size_t(5),size_t(0),size_t(3)})Finish(s,i);s.PendingStage=4;}
  }
  if(s.Stage==4) {
    s.Visible=false;s.Requests.push_back({ApproachMissionAction::DisableVoice,11});return true;
  }
  return false;
}
}
