#pragma once
#include "optionsenv.h"
#include "options.h"
#include "battleselection.h"
namespace Impacto::Saekano {
struct OptionsControlInput {
  std::array<bool,4> ReleasedTabs{};
  bool Close=false,Revert=false,ReleasedRevert=false,PreviousTab=false,NextTab=false;
};
struct OptionsControlServices {
  virtual ~OptionsControlServices()=default;
  virtual void UpdateTabContact(uint32_t tab)=0;
  virtual int32_t UpdatePage(uint32_t tab)=0;
  virtual void UiSound(int32_t id)=0;
  virtual void StopChannel(uint32_t channel)=0;
  virtual int32_t UpdateSampleNavigation(int32_t selected)=0;
  virtual void ResetSamplePage()=0;
  virtual void UpdateRevertContact()=0;
  // Native queries the previous Revert update before collecting its next frame.
  virtual std::optional<bool> RevertReleased(){return std::nullopt;}
};
struct OptionsControlState {
  bool Started=false,SampleNavigationEnabled=false;
  int32_t SelectedTab=0,TabCount=3,SampleSelected=0;
  uint32_t SwitchDuration=0;
  float ArrowTravel=0;
  std::array<float,4> ArrowCurve{};
  TitleTween SampleProgress;
  std::array<TitleTween,2> ArrowProgress;
  std::array<BattleCardMotion,2> Arrows;
  std::array<bool,2> ArrowActive{};
};
// Constructor/init defaults and the original game-utility curve80262040.
void StartOptionsControls(OptionsControlState& state,const OptionsEnvironment& environment,
    bool fourthTabUnlocked,uint64_t now);
// Return values are consumed by UpdateOptions:0 editing,1 close,2 revert.
int32_t UpdateOptionsControls(OptionsControlState& state,const OptionsControlInput& input,
    OptionsControlServices& services,uint64_t now);
}
