#pragma once
#include "optionspages.h"
namespace Impacto::Saekano {
struct OptionsOwnerOutput:OptionsPageOutput {
  virtual void BeginSaveQuestion(const std::u16string& prompt)=0;
  virtual OptionsDialogPoll UpdateSaveQuestion()=0;
  virtual void BeginSystemSave()=0; // Existing native-mode2 worker contract.
  virtual OptionsSavePoll UpdateSystemSave()=0;
  virtual void BeginVoiceReload(uint8_t mode)=0; // Retire7, then load/clear actual menu bank.
  virtual bool VoiceReloadReady()=0;
  virtual bool ForceSilent()const=0;
  virtual void ApplyAudioOptions()=0;
  virtual void RequestTrophy(uint32_t id)=0;
  virtual void TickDecoration(uint64_t now)=0;
};
struct OptionsOwnerState {
  bool Started=false,Finished=false,PreviewHint=false;
  OptionsState Lifecycle;
  OptionsPageState Pages;
  std::u16string SaveQuestion;
};
// Fade duration is the caller's original globalc998 setting, not env_option.
void StartOptionsOwner(OptionsOwnerState& state,const NativeSystemSave& system,
    const OptionsEnvironment& environment,std::span<const int32_t> repeats,
    uint32_t fadeDuration,bool fourthTab,uint64_t now);
int32_t UpdateOptionsOwner(OptionsOwnerState& state,NativeSystemSave& system,
    const OptionsPageInput& input,OptionsOwnerOutput& output,uint64_t now);
}
