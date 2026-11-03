#pragma once
#include "titlecontroller.h"
#include "systemsave.h"

namespace Impacto::Saekano {
// Contiguous native S6022c..S60242: thirteen settings, voice mode, nine toggles.
using OptionsValues=std::array<uint8_t,23>;
OptionsValues ReadOptions(const NativeSystemSave& owner);
void StoreOptions(NativeSystemSave& owner,const OptionsValues& values);
bool OptionsChanged(const OptionsValues& original,const OptionsValues& current);
struct OptionsDialogPoll {bool Busy=true,Cancelled=false;int32_t Selection=0;};
struct OptionsSavePoll {bool Busy=true;uint8_t NativeResult=0;};
// Actual controls, dialogs, storage, voice-resource queue and audio devices are
// owner services. Their results are inputs, never preselected controller phases.
struct OptionsServices {
  virtual ~OptionsServices()=default;
  virtual int32_t UpdateControls()=0; // 0=editing,1=close,2=restore entry snapshot.
  virtual void SynchronizeControls()=0;
  virtual void BeginSaveQuestion()=0;
  virtual OptionsDialogPoll UpdateSaveQuestion()=0;
  virtual void BeginSystemSave()=0;
  virtual OptionsSavePoll UpdateSystemSave()=0;
  virtual void BeginVoiceReload(uint8_t mode)=0;
  virtual bool VoiceReloadReady()=0;
  virtual bool ForceSilent()const=0;
  virtual void ApplyAudioOptions()=0;
  virtual void RequestTrophy(uint32_t id)=0;
  virtual void TickDecoration(uint64_t now)=0;
};
struct OptionsState {
  bool Started=false;
  int32_t Phase=0;
  std::optional<int32_t> PendingPhase;
  OptionsValues Original{};
  TitleTween Fade;
};
void StartOptions(OptionsState& state,const OptionsValues& current,uint32_t fadeDuration,uint64_t now);
// Returns1 at native phase13. Caller retires the owner then.
int32_t UpdateOptions(OptionsState& state,OptionsValues& current,OptionsServices& host,uint64_t now);
}
