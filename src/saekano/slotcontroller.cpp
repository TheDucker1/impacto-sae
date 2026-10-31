#include "slotcontroller.h"
#include "commands.h"

namespace Impacto::Saekano {
namespace {
void Commit(int32_t& value,std::optional<int32_t>& pending){if(pending){value=*pending;pending.reset();}}
int32_t UpdateLoad(SlotControllerState& s,SlotControllerServices& h){
  Commit(s.OperationPhase,s.PendingOperationPhase);
  if(s.OperationPhase==0){
    if(!h.SelectedOccupied())return 2;
    h.BeginDialog(SlotPrompt::Load);s.DialogPresent=true;s.OperationPhase=1;
  }
  if(s.OperationPhase==1){
    const auto poll=h.UpdateDialog();
    if(!poll.Visible){
      if(poll.Selection!=1 || poll.Cancelled)return 2;
      s.OperationPhase=2;
    }
  }
  if(s.OperationPhase==2){
    h.StageSelectedLoad();h.BeginDialog(SlotPrompt::Loading);s.DialogPresent=true;s.OperationPhase=3;
  }
  if(s.OperationPhase==3 && !h.UpdateDialog().Visible){
    h.RetireDialog();s.DialogPresent=false;return 1;
  }
  return 0;
}
}
void StartSlotController(SlotControllerState& s,int32_t mode,uint32_t duration,uint64_t now){
  if(mode!=0 && mode!=1)throw FormatError("slot mode outside save/load");
  s={};s.Started=true;s.RequestedMode=mode;s.FadeDuration=duration;s.Fade.Configure(0,0,0,false,now);
}
int32_t UpdateSlotController(SlotControllerState& s,SlotControllerServices& h,uint64_t now){
  if(!s.Started)throw UnsupportedCommand("slot controller has no initialized owner");
  Commit(s.Phase,s.PendingPhase);
  const auto request=[&](int32_t p,bool immediate=false){if(immediate){s.Phase=p;s.PendingPhase.reset();}else s.PendingPhase=p;};
  if(s.Phase==0)request(1,true);
  if(s.Phase==1)request(2,true);
  if(s.Phase==2){s.Mode=s.RequestedMode;s.RequestedMode=-1;request(3);}
  if(s.Phase==3)request(4,true);
  if(s.Phase==4){
    const auto action=h.UpdateBrowser();
    if(action==4)request(5);
    else if(action==5){h.UiSound(4);request(13);}
    else if(action==8)request(7);
    else if(action!=3)request(1,true);
  }
  if(s.Phase==5){
    if(s.Mode==0)h.BeginSlotSave();
    s.OperationPhase=0;s.PendingOperationPhase.reset();request(6);
  }
  if(s.Phase==6){
    if(s.Mode==1){const auto result=UpdateLoad(s,h);if(result==2)request(4);else if(result){s.Loaded=true;request(11);}}
    else if(s.Mode==0 && h.UpdateSlotSave())request(4);
  }
  if(s.Phase==7){
    if(h.SelectedOccupied()){h.BeginDialog(h.SelectedProtected()?SlotPrompt::Unprotect:SlotPrompt::Protect);s.DialogPresent=true;request(8);}
    else request(4);
  }
  if(s.Phase==8){
    const auto poll=h.UpdateDialog();
    if(!poll.Visible){
      if(poll.Selection==1){h.SetSelectedProtected(!h.SelectedProtected());request(9);}else request(4);
      h.RetireDialog();s.DialogPresent=false;
    }
  }
  if(s.Phase==9){h.BeginSystemSave();request(10);}
  if(s.Phase==10){
    const auto poll=h.UpdateSystemSave();
    if(!poll.Busy){
      if(!poll.Accepted){
        h.SetSelectedProtected(!h.SelectedProtected());
        // Native requests8 after destroying its modal. Recreate the same
        // confirmation before that retry, instead of dereferencing a null owner.
        h.BeginDialog(h.SelectedProtected()?SlotPrompt::Unprotect:SlotPrompt::Protect);s.DialogPresent=true;request(8,true);
      }else request(4);
    }
  }
  if(s.Phase==11){s.Fade.Configure(0,255,s.FadeDuration,true,now);request(12,true);}
  if(s.Phase==12){s.Fade.Tick(now);if(s.Fade.Finished())request(13,true);}
  if(s.Phase==13)request(14,true);
  if(s.Phase==14)return s.Loaded?3:2;
  h.TickHelp();return 0;
}
}
