#include "optionspages.h"
#include "commands.h"
namespace Impacto::Saekano {
namespace {
struct PageAdapter final:OptionsControlServices,OptionsTextServices,OptionsAudioServices,OptionsVoiceServices {
  OptionsPageState& S;NativeSystemSave& System;OptionsPageInput Input;OptionsPageOutput& Output;uint64_t Now;
  uint32_t Page=0,Slider=0;
  PageAdapter(OptionsPageState& s,NativeSystemSave& system,const OptionsPageInput& in,OptionsPageOutput& out,uint64_t now):S(s),System(system),Input(in),Output(out),Now(now){}
  void Sync(){StoreOptions(System,S.Values);}
  void Widget(int32_t page,OptionsWidgetKind kind,uint32_t index=0,int32_t x=0,int32_t y=0){
    Sync();const auto contact=Output.CollectWidget({page,kind,index},x,y,Now);if(!contact)return;
    const auto& c=*contact;
    switch(kind){
      case OptionsWidgetKind::Tab:Input.Common.ReleasedTabs.at(index)=c.Released;break;
      case OptionsWidgetKind::Revert:break; // Queried separately before this update.
      case OptionsWidgetKind::Slider:
        if(page==0)Input.Text.Sliders.at(index)={c.Pressed,c.Held,c.Released,c.PointerX};
        else Input.Audio.Sliders.at(index)={c.Pressed,c.Held,c.Released,c.PointerX};break;
      case OptionsWidgetKind::Background:
        if(page==0)Input.Text.ReleasedBackground=c.Released;else Input.Audio.ReleasedBackground=c.Released;break;
      case OptionsWidgetKind::Toggle:
        if(page==0)Input.Text.ReleasedToggles.at(index)=c.Released;else Input.Audio.ReleasedToggle=c.Released;break;
      case OptionsWidgetKind::Label:
        if(page==0)Input.Text.ReleasedLabels.at(index)=c.Released;else Input.Audio.ReleasedLabels.at(index)=c.Released;break;
      case OptionsWidgetKind::Mode:Input.Voice.ReleasedModes.at(index)=c.Released;break;
      case OptionsWidgetKind::CharacterToggle:Input.Voice.ReleasedToggles.at(index)=c.Released;break;
      case OptionsWidgetKind::CharacterLabel:Input.Voice.ReleasedCharacterLabels.at(index)=c.Released;break;
      case OptionsWidgetKind::GroupLabel:Input.Voice.ReleasedGroupLabels.at(index)=c.Released;break;
    }
  }
  void UpdateTabContact(uint32_t tab)override{Widget(-1,OptionsWidgetKind::Tab,tab);}
  void UpdateRevertContact()override{Widget(-1,OptionsWidgetKind::Revert);}
  std::optional<bool> RevertReleased()override{
    Sync();const auto contact=Output.QueryWidgetContact({-1,OptionsWidgetKind::Revert,0});
    if(contact)return contact->Released;return std::nullopt;
  }
  void UiSound(int32_t id)override{Sync();Output.UiSound(id);}
  void StopChannel(uint32_t ch)override{Sync();Output.StopChannel(ch);}
  bool UpdateNavigation(OptionsRowNavigation& rows)override{
    const bool any=UpdateOptionsNavigation(S.Navigators.at(Page),rows,Input.Vertical);
    for(uint32_t i=0;i<S.Navigators[Page].Sounds;++i)UiSound(2);return any;
  }
  int32_t UpdatePage(uint32_t page)override{
    Page=page;
    if(page==0){auto& in=Input.Text;in.Pressed=Input.HorizontalPressed;in.Held=Input.HorizontalHeld;S.Text.RepeatCounters=S.HorizontalCounters;
      const auto result=UpdateOptionsText(S.Text,S.Rows[0],S.Values,in,*this);S.HorizontalCounters=S.Text.RepeatCounters;return result;}
    if(page==1){auto& in=Input.Audio;in.Pressed=Input.HorizontalPressed;in.Held=Input.HorizontalHeld;in.PreviewPressed=Input.Preview;S.Audio.RepeatCounters=S.HorizontalCounters;
      const auto result=UpdateOptionsAudio(S.Audio,S.Rows[1],S.Values,in,S.Voice.Mode,*this);S.HorizontalCounters=S.Audio.RepeatCounters;return result;}
    if(page==2){auto& in=Input.Voice;in.Pressed={Input.Vertical.Pressed[0],Input.Vertical.Pressed[1],Input.HorizontalPressed[0],Input.HorizontalPressed[1],Input.Accept};
      return UpdateOptionsVoice(S.Voice,S.Rows[2],S.Controls.SampleProgress,S.Values,in,*this,Now);}
    throw FormatError("Options page dispatch outside native handlers");
  }
  int32_t UpdateSampleNavigation(int32_t selected)override{Sync();return Output.UpdateSampleNavigation(selected);}
  void ResetSamplePage()override{Sync();Output.ResetSamplePage();}
  void UpdateSliderWidget(uint32_t i,int32_t x,int32_t y)override{Widget(int32_t(Page),OptionsWidgetKind::Slider,i,x,y);}
  void BeginSliderWidget(uint32_t i)override{Slider=i;}
  void UpdateWidget(int32_t x,int32_t y)override{UpdateSliderWidget(Slider,x,y);}
  void UpdateBackgroundWidget()override{Widget(int32_t(Page),OptionsWidgetKind::Background);}
  void UpdateToggleWidget(uint32_t i)override{Widget(int32_t(Page),OptionsWidgetKind::Toggle,i);}
  void UpdateToggleWidget()override{UpdateToggleWidget(0);}
  void UpdateLabelWidget(uint32_t i)override{Widget(int32_t(Page),OptionsWidgetKind::Label,i);}
  void SetToggleVariant(uint32_t i,bool inverted)override{Sync();Output.SetToggleVariant(Page,i,inverted);}
  void SetToggleVariant(bool inverted)override{SetToggleVariant(0,inverted);}
  void UpdateModeWidget(uint32_t i)override{Widget(2,OptionsWidgetKind::Mode,i);}
  void UpdateCharacterToggle(uint32_t i)override{Widget(2,OptionsWidgetKind::CharacterToggle,i);}
  void UpdateCharacterLabel(uint32_t i)override{Widget(2,OptionsWidgetKind::CharacterLabel,i);}
  void UpdateGroupLabel(uint32_t i)override{Widget(2,OptionsWidgetKind::GroupLabel,i);}
  void SetCharacterVariant(uint32_t i,bool inverted)override{SetToggleVariant(i,inverted);}
  void SetBgmVolume(int32_t p)override{Sync();Output.SetBgmVolume(p);}
  void SetSeVolume(int32_t p)override{Sync();Output.SetSeVolume(p);}
  void SetUiVolume(int32_t p)override{Sync();Output.SetUiVolume(p);}
  void SetChannelVolume(uint32_t ch,int32_t p)override{Sync();Output.SetChannelVolume(ch,p);}
  void SetDuckingVolume(int32_t p)override{Sync();Output.SetDuckingVolume(p);}
  void PrepareSystemPreview(uint32_t ch,int32_t mode)override{Sync();Output.PrepareSystemPreview(ch,mode);}
  void StartSystemPreview(uint32_t ch)override{Sync();Output.StartSystemPreview(ch);}
};
}
void StartOptionsPages(OptionsPageState& s,const NativeSystemSave& system,const OptionsEnvironment& e,std::span<const int32_t> repeats,bool fourth,uint64_t now){
  s={};s.Values=ReadOptions(system);StartOptionsControls(s.Controls,e,fourth,now);StartOptionsPageNavigators(s.Navigators,s.Rows,e,repeats);
  StartOptionsText(s.Text,e,s.Values,repeats);StartOptionsAudio(s.Audio,e,s.Values,repeats);StartOptionsVoice(s.Voice,e,s.Values);s.Started=true;
}
int32_t UpdateOptionsPages(OptionsPageState& s,NativeSystemSave& system,const OptionsPageInput& in,OptionsPageOutput& out,uint64_t now){
  if(!s.Started)throw UnsupportedCommand("Options pages have no shared owner");PageAdapter adapter(s,system,in,out,now);
  const auto result=UpdateOptionsControls(s.Controls,adapter.Input.Common,adapter,now);StoreOptions(system,s.Values);return result;
}
}
