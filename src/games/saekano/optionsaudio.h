#pragma once
#include "optionsslider.h"
#include "optionsenv.h"
namespace Impacto::Saekano {
struct OptionsAudioInput {
  std::array<OptionsSliderInput,5> Sliders;
  bool ReleasedBackground=false,ReleasedToggle=false,PreviewPressed=false;
  std::array<bool,6> ReleasedLabels{};
  std::array<bool,2> Pressed{},Held{};
};
struct OptionsAudioState {
  bool Started=false;
  std::array<OptionsSliderLayout,5> Layouts;
  std::array<OptionsSliderState,5> Sliders;
  int32_t Maximum=10;
  std::array<int32_t,2> RepeatCounters{};
  std::vector<int32_t> RepeatIntervals;
};
struct OptionsAudioServices:OptionsSliderServices {
  virtual bool UpdateNavigation(OptionsRowNavigation& navigation)=0;
  virtual void BeginSliderWidget(uint32_t index)=0; // Selects the real widget for UpdateWidget.
  virtual void UpdateBackgroundWidget()=0;
  virtual void UpdateToggleWidget()=0;
  virtual void UpdateLabelWidget(uint32_t index)=0;
  virtual void SetToggleVariant(bool inverted)=0;
  virtual void PrepareSystemPreview(uint32_t channel,int32_t mode)=0;
  virtual void StartSystemPreview(uint32_t channel)=0;
};
void StartOptionsAudio(OptionsAudioState& state,const OptionsEnvironment& environment,
    const OptionsValues& values,std::span<const int32_t> repeatIntervals);
// voiceMode is the shared native selectorba8, also edited by the voice page.
int32_t UpdateOptionsAudio(OptionsAudioState& state,OptionsRowNavigation& navigation,
    OptionsValues& values,const OptionsAudioInput& input,int32_t voiceMode,OptionsAudioServices& services);
}
