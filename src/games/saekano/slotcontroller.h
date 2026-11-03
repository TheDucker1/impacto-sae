#pragma once
#include "titlecontroller.h"

namespace Impacto::Saekano {
enum class SlotPrompt { Load, Loading, Protect, Unprotect };
struct SlotDialogPoll { bool Visible=true,Cancelled=false;int32_t Selection=-1; };
struct SlotWritePoll { bool Busy=true,Accepted=false; };
// Actual navigation, modal, record-copy and storage owners supply these services.
// Their results are observed inputs, never controller phases assigned by a host.
struct SlotControllerServices {
  virtual ~SlotControllerServices()=default;
  virtual int32_t UpdateBrowser()=0;
  virtual void UiSound(int32_t id)=0;
  virtual bool SelectedOccupied()const=0;
  virtual bool SelectedProtected()const=0;
  virtual void SetSelectedProtected(bool value)=0;
  virtual void BeginDialog(SlotPrompt prompt)=0;
  virtual SlotDialogPoll UpdateDialog()=0;
  virtual void RetireDialog()=0;
  virtual void StageSelectedLoad()=0; // Native record copy, pending protected=0, load-request=1.
  virtual void BeginSlotSave()=0;
  virtual bool UpdateSlotSave()=0;
  virtual void BeginSystemSave()=0;
  virtual SlotWritePoll UpdateSystemSave()=0;
  virtual void TickNewMarker()=0; // Native1ac02 on the shared3990 marker owner.
};
struct SlotControllerState {
  bool Started=false,Loaded=false,DialogPresent=false;
  int32_t Phase=0,Mode=-1,RequestedMode=1,OperationPhase=0;
  std::optional<int32_t> PendingPhase,PendingOperationPhase;
  uint32_t FadeDuration=0;
  TitleTween Fade;
};
// Native59d18/5bd3a control fields after initialization; actual browser setup is
// separate. mode0 saves, mode1 loads. Title passes1 and context byte1.
void StartSlotController(SlotControllerState& state,int32_t mode,uint32_t fadeDuration,uint64_t now);
// Native5ca4c, including5c32a load confirmation/copy/message sequencing.
// Returns0 busy,2 canceled,3 loaded. Caller retires the finished owner.
int32_t UpdateSlotController(SlotControllerState& state,SlotControllerServices& host,uint64_t now);
}
