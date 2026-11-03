#pragma once
#include "optionscontrols.h"
#include "optionsnavigation.h"
#include "optionstext.h"
#include "optionsaudio.h"
#include "optionsvoice.h"
#include <optional>
namespace Impacto::Saekano {
enum class OptionsWidgetKind { Tab,Revert,Slider,Background,Toggle,Label,Mode,CharacterToggle,CharacterLabel,GroupLabel };
struct OptionsWidgetId {int32_t Page=-1;OptionsWidgetKind Kind=OptionsWidgetKind::Tab;uint32_t Index=0;};
struct OptionsWidgetContact {bool Pressed=false,Held=false,Released=false;int32_t PointerX=0;};
struct OptionsPageInput {
  OptionsControlInput Common;
  OptionsNavigationInput Vertical;
  std::array<bool,2> HorizontalPressed{},HorizontalHeld{};
  bool Accept=false,Preview=false;
  // Contacts only. Page-specific key fields are replaced by the common keys.
  OptionsTextInput Text;
  OptionsAudioInput Audio;
  OptionsVoiceInput Voice;
};
struct OptionsPageOutput {
  virtual ~OptionsPageOutput()=default;
  virtual void UpdateWidget(OptionsWidgetId widget,int32_t x,int32_t y)=0;
  // A live input host updates/queries the concrete widget here. Null retains
  // explicit contact fixtures; no widget is sampled ahead of the native branch.
  virtual std::optional<OptionsWidgetContact> CollectWidget(OptionsWidgetId widget,
      int32_t x,int32_t y,uint64_t){UpdateWidget(widget,x,y);return std::nullopt;}
  virtual std::optional<OptionsWidgetContact> QueryWidgetContact(OptionsWidgetId){return std::nullopt;}
  virtual void SetToggleVariant(uint32_t page,uint32_t index,bool inverted)=0;
  virtual void UiSound(int32_t id)=0;
  virtual void StopChannel(uint32_t channel)=0;
  virtual void SetBgmVolume(int32_t percent)=0;
  virtual void SetSeVolume(int32_t percent)=0;
  virtual void SetUiVolume(int32_t percent)=0;
  virtual void SetChannelVolume(uint32_t channel,int32_t percent)=0;
  // Attenuation setting, separate from UI channel6 and ordinary voice channels.
  virtual void SetDuckingVolume(int32_t percent)=0;
  virtual void PrepareSystemPreview(uint32_t channel,int32_t mode)=0;
  virtual void StartSystemPreview(uint32_t channel)=0;
  // Separate text-sample owner; dormant in the fresh Options owner.
  virtual int32_t UpdateSampleNavigation(int32_t selected)=0;
  virtual void ResetSamplePage()=0;
};
struct OptionsPageState {
  bool Started=false;
  OptionsValues Values{};
  OptionsControlState Controls;
  OptionsTextState Text;
  OptionsAudioState Audio;
  OptionsVoiceState Voice;
  std::array<OptionsNavigationState,3> Navigators;
  std::array<OptionsRowNavigation,3> Rows;
  std::array<int32_t,2> HorizontalCounters{}; // Nativeff8/ff0, shared across text/audio.
};
void StartOptionsPages(OptionsPageState& state,const NativeSystemSave& system,
    const OptionsEnvironment& environment,std::span<const int32_t> repeats,bool fourthTab,uint64_t now);
int32_t UpdateOptionsPages(OptionsPageState& state,NativeSystemSave& system,
    const OptionsPageInput& input,OptionsPageOutput& output,uint64_t now);
}
