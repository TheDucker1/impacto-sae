#include "titlenavigation.h"
#include <algorithm>
#include <bit>

namespace Impacto::Saekano {
TitleNavigation::TitleNavigation(std::span<const int32_t> repeatIntervals)
    :repeats_(repeatIntervals.begin(),repeatIntervals.end()) {
  // The native loop reads end[-1] for an empty table and can fail to terminate
  // for nonpositive intervals. Reject those inputs before starting an owner.
  if(repeats_.empty() || repeats_.size()>1000000 ||
      std::any_of(repeats_.begin(),repeats_.end(),[](auto n){return n<=0;}))
    throw FormatError("invalid title navigation repeat intervals");
}
bool TitleNavigation::Repeats(int32_t counter)const {
  if(counter<=0)return false;
  int64_t sum=0;
  for(auto interval:repeats_){sum+=interval;if(sum>=counter)return sum==counter;}
  // Native repeats its final interval indefinitely. Use a remainder rather
  // than a per-interval walk, preserving the exact integer deadline.
  return (int64_t(counter)-sum)%repeats_.back()==0;
}
const TitleNavigationState& TitleNavigation::Update(int32_t selected,const TitleNavigationInput& input) {
  if(selected<0 || selected>3)throw FormatError("title navigator selection outside four rows");
  state_.Selected=selected;state_.Activated={};state_.Sounds=0;
  for(size_t i=0;i<2;++i){
    auto& counter=state_.Counters[i];bool active=false;
    if(counter==-1){} // Native edge latch is rearmed only after key release.
    else if(input.Pressed[i]){counter=0;active=true;}
    else if(!input.Held[i])counter=0;
    else {counter=std::bit_cast<int32_t>(uint32_t(counter)+1);active=Repeats(counter);}
    if(active){
      state_.Previous=state_.Selected;
      state_.Selected=(state_.Selected+(i==0?3:1))%4;
      state_.Activated[i]=true;++state_.Sounds;
      if(state_.Selected==(i==0?0:3))counter=-1;
    }
    if(counter==-1 && !input.Held[i])counter=0;
  }
  return state_;
}
}
