#pragma once
#include "optionsslider.h"
#include "optionsenv.h"
namespace Impacto::Saekano {
struct OptionsTextInput {
  std::array<OptionsSliderInput,3> Sliders;
  bool ReleasedBackground=false;
  std::array<bool,4> ReleasedToggles{};
  std::array<bool,7> ReleasedLabels{};
  std::array<bool,2> Pressed{},Held{}; // right/key13, left/key12.
};
struct OptionsTextState {
  bool Started=false;
  std::array<OptionsSliderLayout,3> Layouts;
  std::array<OptionsSliderState,3> Sliders;
  std::array<int32_t,3> Maximum{};
  std::array<int32_t,2> RepeatCounters{};
  std::vector<int32_t> RepeatIntervals;
};
struct OptionsTextServices {
  virtual ~OptionsTextServices()=default;
  virtual bool UpdateNavigation(OptionsRowNavigation& navigation)=0; // native activation flag588, not selection difference.
  virtual void UpdateSliderWidget(uint32_t index,int32_t x,int32_t y)=0;
  virtual void UpdateBackgroundWidget()=0;
  virtual void UpdateToggleWidget(uint32_t index)=0;
  virtual void UpdateLabelWidget(uint32_t index)=0;
  virtual void SetToggleVariant(uint32_t index,bool inverted)=0;
  virtual void UiSound(int32_t id)=0;
};
void StartOptionsText(OptionsTextState& state,const OptionsEnvironment& environment,
    const OptionsValues& values,std::span<const int32_t> repeatIntervals);
int32_t UpdateOptionsText(OptionsTextState& state,OptionsRowNavigation& navigation,
    OptionsValues& values,const OptionsTextInput& input,OptionsTextServices& services);
}
