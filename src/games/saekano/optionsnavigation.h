#pragma once
#include "optionsslider.h"
#include "optionsenv.h"
namespace Impacto::Saekano {
struct OptionsNavigationInput {std::array<bool,2> Pressed{},Held{};}; // up10,down11.
struct OptionsNavigationState {
  bool Started=false,Enabled=true,Continuous=false,Wrap=true,SoundEnabled=true;
  std::array<int32_t,2> Counters{};
  std::array<bool,2> Activated{};
  bool Any=false;
  uint32_t Sounds=0;
  std::vector<int32_t> RepeatIntervals;
};
// The configured vertical path of native8018ed28/8019c554 used by Options.
// Minimum is0. Arbitrary horizontal utility navigation is a separate path.
void StartOptionsNavigation(OptionsNavigationState& state,OptionsRowNavigation& rows,
    uint32_t count,uint32_t visible,uint32_t margin,std::span<const int32_t> repeatIntervals,bool continuous=false);
bool UpdateOptionsNavigation(OptionsNavigationState& state,OptionsRowNavigation& rows,const OptionsNavigationInput& input);
// Exact three constructor spans from the authored text/audio/voice collections.
void StartOptionsPageNavigators(std::array<OptionsNavigationState,3>& states,
    std::array<OptionsRowNavigation,3>& rows,const OptionsEnvironment& environment,std::span<const int32_t> repeatIntervals);
}
