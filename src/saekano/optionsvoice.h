#pragma once
#include "optionsslider.h"
#include "optionsenv.h"
namespace Impacto::Saekano {
struct OptionsVoiceState {bool Started=false;int32_t Mode=0,Character=0;uint32_t HighlightDuration=0;int32_t HighlightTarget=0;};
struct OptionsVoiceInput {
  std::array<bool,5> ReleasedModes{};
  std::array<bool,9> ReleasedToggles{},ReleasedCharacterLabels{};
  std::array<bool,2> ReleasedGroupLabels{};
  std::array<bool,5> Pressed{}; // up10,down11,right13,left12,accept1.
};
struct OptionsVoiceServices {
  virtual ~OptionsVoiceServices()=default;
  virtual void UpdateModeWidget(uint32_t index)=0;
  virtual void UpdateCharacterToggle(uint32_t index)=0;
  virtual void UpdateCharacterLabel(uint32_t index)=0;
  virtual void UpdateGroupLabel(uint32_t index)=0;
  virtual void SetCharacterVariant(uint32_t index,bool inverted)=0;
  virtual void UiSound(int32_t id)=0;
  virtual void PrepareSystemPreview(uint32_t channel,int32_t mode)=0;
  virtual void StartSystemPreview(uint32_t channel)=0;
  virtual void StopChannel(uint32_t channel)=0;
  virtual bool UpdateNavigation(OptionsRowNavigation& navigation)=0;
};
void StartOptionsVoice(OptionsVoiceState& state,const OptionsEnvironment& environment,const OptionsValues& values);
// Highlight is the common OptionsControlState.SampleProgress (nativebd0), not
// a separate page-owned animation. Mode also feeds the audio preview selector.
int32_t UpdateOptionsVoice(OptionsVoiceState& state,OptionsRowNavigation& navigation,
    TitleTween& highlight,OptionsValues& values,const OptionsVoiceInput& input,
    OptionsVoiceServices& services,uint64_t now);
}
