#include "savemanager.h"
#include "commands.h"

namespace Impacto::Saekano {
SaveManagerEnvironment SaveManagerEnvironment::Decode(std::span<const uint8_t> bytes) {
  PayloadReader r(bytes);SaveManagerEnvironment env;
  for(auto& f:env.Flags)f=r.U8();for(auto& text:env.Strings)text=r.String();
  if(r.Remaining())throw FormatError("unexpected save-manager environment tail");
  return env;
}
const std::u16string& SaveManagerEnvironment::Mode2Message(int32_t nativeCase)const {
  switch(nativeCase) {
    case 2:return Strings[44]; // 8025ff5c, saving progress, no buttons.
    case 4:return Strings[55]; // 802601c4, access error, group0.
    case 5:return Strings[52]; // 8026011c, saved, group0.
    default:throw UnsupportedCommand("unsupported mode2 save message");
  }
}
void BeginMode2Save(SaveWorkerState& s) {
  if(s.Active)throw FormatError("save worker is already active");
  s={};s.Started=true;s.Active=true;
}
void UpdateMode2Save(SaveWorkerState& s,SaveWorkerServices& h,uint64_t now) {
  if(!s.Started)throw UnsupportedCommand("save worker Begin was suppressed");
  if(!s.Active)return;
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  if(s.Phase==0)s.PendingPhase=1;
  if(s.Phase==1){h.ShowMessage(1,now);s.PendingPhase=2;}
  if(s.Phase==2){h.CloseMessage();h.ShowMessage(2,now);s.PendingPhase=3;}
  if(s.Phase==3 && h.DialogReady()) {
    h.Snapshot(SaveWriteStage::System);h.BeginWrite(SaveWriteStage::System);s.PendingPhase=4;
  }
  const auto complete=[&](int32_t successPhase) {
    const auto io=h.PollWrite();if(!io.Done)return;
    if(io.Error==0){s.Accepted=true;s.Phase=successPhase;}
    else {
      s.Accepted=false;
      if(io.Error==0x80100642 || io.Error==0x80100643)
        throw UnsupportedCommand("Vita save repair flow is not bound");
      h.CloseMessage();h.ShowMessage(4,now);s.PendingPhase=7;
    }
  };
  if(s.Phase==4)complete(5);
  if(s.Phase==5 && h.DialogReady()) {
    h.Snapshot(SaveWriteStage::Slots);h.BeginWrite(SaveWriteStage::Slots);s.PendingPhase=6;
  }
  if(s.Phase==6)complete(12);
  if(s.Phase==7 && !h.DialogVisible())s.PendingPhase=1;
  if(s.Phase==12){h.CloseMessage();h.ShowMessage(5,now);s.PendingPhase=19;}
  if(s.Phase==19 && !h.DialogVisible()) {
    s.Phase=0;s.PendingPhase.reset();s.Active=false;return;
  }
  h.UpdateMessage(now);
}
SaveManagerDialogHost::SaveManagerDialogHost(SaveManagerEnvironment environment,
    LayoutFactory layout,SaveWriteServices& storage):environment_(std::move(environment)),
    buildLayout_(std::move(layout)),storage_(storage) {
  if(!buildLayout_)throw FormatError("missing save dialog layout service");
}
void SaveManagerDialogHost::Begin(int32_t nativeMode,uint64_t) {
  if(nativeMode!=2)throw UnsupportedCommand("unsupported ending save mode");
  BeginMode2Save(state_);Modal={};Input={};prompt_.clear();layout_={};
}
EndingSaveStatus SaveManagerDialogHost::Update(uint64_t now) {
  UpdateMode2Save(state_,*this,now);return {state_.Active,state_.Accepted};
}
void SaveManagerDialogHost::ShowMessage(int32_t nativeCase,uint64_t now) {
  // Case1 in original8001ce2c only binds the context and destroys a temporary.
  if(nativeCase==1)return;
  prompt_=environment_.Mode2Message(nativeCase);
  const size_t buttons=nativeCase==2?0:1;
  layout_=buildLayout_(prompt_,buttons);
  BeginDialog(Modal,layout_,buttons,0,false,true,now);
}
void SaveManagerDialogHost::UpdateMessage(uint64_t now) {
  UpdateDialog(Modal,Input,now);Input={};
}
}
