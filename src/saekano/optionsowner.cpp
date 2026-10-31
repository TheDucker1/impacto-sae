#include "optionsowner.h"
#include "commands.h"
namespace Impacto::Saekano {
namespace {
struct OwnerAdapter final:OptionsServices {
  OptionsOwnerState& S;NativeSystemSave& System;const OptionsPageInput& Input;OptionsOwnerOutput& Output;uint64_t Now;
  OwnerAdapter(OptionsOwnerState& s,NativeSystemSave& system,const OptionsPageInput& in,OptionsOwnerOutput& out,uint64_t now):S(s),System(system),Input(in),Output(out),Now(now){}
  void Sync()const{StoreOptions(System,S.Pages.Values);}
  int32_t UpdateControls()override{return UpdateOptionsPages(S.Pages,System,Input,Output,Now);}
  void SynchronizeControls()override{
    // Native547b8 restores pixel positions only; selector/navigation/highlight
    // state survives until this closing owner is retired.
    for(size_t i=0;i<3;++i){const auto& l=S.Pages.Text.Layouts[i];const auto index=i==2?3:i;S.Pages.Text.Sliders[i].X=int32_t(int64_t(l.OriginX)+int64_t(l.Step)*S.Pages.Values[index]);}
    for(size_t i=0;i<5;++i){const auto& l=S.Pages.Audio.Layouts[i];S.Pages.Audio.Sliders[i].X=int32_t(int64_t(l.OriginX)+int64_t(l.Step)*S.Pages.Values[7+i]);}
    Sync();
  }
  void BeginSaveQuestion()override{Sync();Output.BeginSaveQuestion(S.SaveQuestion);}
  OptionsDialogPoll UpdateSaveQuestion()override{Sync();return Output.UpdateSaveQuestion();}
  void BeginSystemSave()override{Sync();Output.BeginSystemSave();}
  OptionsSavePoll UpdateSystemSave()override{Sync();return Output.UpdateSystemSave();}
  void BeginVoiceReload(uint8_t mode)override{Sync();Output.BeginVoiceReload(mode);}
  bool VoiceReloadReady()override{Sync();return Output.VoiceReloadReady();}
  bool ForceSilent()const override{Sync();return Output.ForceSilent();}
  void ApplyAudioOptions()override{Sync();Output.ApplyAudioOptions();}
  void RequestTrophy(uint32_t id)override{Sync();Output.RequestTrophy(id);}
  void TickDecoration(uint64_t now)override{
    Sync();Output.TickDecoration(now);
    S.PreviewHint=S.Pages.Controls.SelectedTab==1 && S.Pages.Rows[1].Selected==3;
  }
};
}
void StartOptionsOwner(OptionsOwnerState& s,const NativeSystemSave& system,const OptionsEnvironment& e,std::span<const int32_t> repeats,uint32_t duration,bool fourth,uint64_t now){
  s={};StartOptionsPages(s.Pages,system,e,repeats,fourth,now);StartOptions(s.Lifecycle,s.Pages.Values,duration,now);s.SaveQuestion=e.SaveQuestion;s.Started=true;
}
int32_t UpdateOptionsOwner(OptionsOwnerState& s,NativeSystemSave& system,const OptionsPageInput& in,OptionsOwnerOutput& out,uint64_t now){
  if(!s.Started || s.Finished)throw UnsupportedCommand("Options owner is not active");OwnerAdapter adapter(s,system,in,out,now);
  const auto result=UpdateOptions(s.Lifecycle,s.Pages.Values,adapter,now);StoreOptions(system,s.Pages.Values);s.Finished=result==1;return result;
}
}
