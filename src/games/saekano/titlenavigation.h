#pragma once
#include "formats.h"
#include <array>

namespace Impacto::Saekano {
struct TitleNavigationInput {
  std::array<bool,2> Pressed{},Held{}; // Native logical keys10/11: previous/next.
};
struct TitleNavigationState {
  int32_t Selected=0,Previous=0;
  std::array<int32_t,2> Counters{};
  std::array<bool,2> Activated{};
  uint32_t Sounds=0;
};
// The title's native navigator configuration: vertical only, four visible rows,
// bounds0..3, step1, wrap enabled, UI sound2 on selection changes. The repeat
// intervals come from utility global80262888, not the platform's key-repeat.
class TitleNavigation {
 public:
  explicit TitleNavigation(std::span<const int32_t> repeatIntervals);
  const TitleNavigationState& Update(int32_t selected,const TitleNavigationInput& input);
  const TitleNavigationState& State()const{return state_;}
 private:
  bool Repeats(int32_t counter)const;
  std::vector<int32_t> repeats_;
  TitleNavigationState state_;
};
}
