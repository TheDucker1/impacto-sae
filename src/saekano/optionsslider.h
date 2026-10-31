#pragma once
#include "options.h"
namespace Impacto::Saekano {
struct OptionsRowNavigation {
  int32_t Scroll=0,Selected=0,Minimum=0,Maximum=0,Visible=1,Margin=0;
  int32_t PreviousSelected=0,PreviousScroll=0;
};
void SelectOptionsRow(OptionsRowNavigation& navigation,int32_t row);
enum class OptionsSliderKind { Text,Audio };
struct OptionsSliderLayout {
  OptionsSliderKind Kind=OptionsSliderKind::Text;
  int32_t Row=0,OriginX=0,Y=0,Step=1,Width=0;
};
struct OptionsSliderInput {bool Pressed=false,Held=false,Released=false;int32_t PointerX=0;};
struct OptionsSliderState {int32_t X=0,Value=0;bool KeyboardActive=true;};
struct OptionsSliderServices {
  virtual ~OptionsSliderServices()=default;
  virtual void UpdateWidget(int32_t x,int32_t y)=0;
  virtual void UiSound(int32_t id)=0;
  virtual void SetBgmVolume(int32_t percent)=0;
  virtual void SetSeVolume(int32_t percent)=0;
  virtual void SetUiVolume(int32_t percent)=0;
  virtual void SetChannelVolume(uint32_t channel,int32_t percent)=0;
  // Native aa83c sets the attenuation percentage consumed by mixer b6602.
  virtual void SetDuckingVolume(int32_t percent)=0;
};
void ApplyOptionsVolume(int32_t row,int32_t percent,OptionsSliderServices& services);
// Native56374/552fc. Contacts are already sampled from the shared input owner;
// the frontend uses UpdateWidget to retain the native pre-update handle position.
int32_t UpdateOptionsSlider(OptionsSliderState& state,OptionsRowNavigation& navigation,
    OptionsValues& values,const OptionsSliderLayout& layout,const OptionsSliderInput& input,
    OptionsSliderServices& services);
}
