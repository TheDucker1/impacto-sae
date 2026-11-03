#pragma once
#include "formats.h"
#include <algorithm>
#include <array>
#include <bit>
#include <span>
namespace Impacto::Saekano {
inline void ValidateOptionsRepeatIntervals(std::span<const int32_t> intervals){
  if(intervals.empty() || intervals.size()>1000000 || std::any_of(intervals.begin(),intervals.end(),[](int32_t v){return v<=0;}))throw FormatError("invalid Options repeat intervals");
}
// Call after validating the owner intervals.
inline bool OptionsRepeatDeadline(int32_t counter,std::span<const int32_t> intervals){
  if(counter<=0)return counter==0;
  int64_t sum=0;for(auto interval:intervals){sum+=interval;if(sum>=counter)return sum==counter;}
  return (int64_t(counter)-sum)%intervals.back()==0;
}
// Right firing deliberately leaves the left counter untouched, including on
// an endpoint-clamped change.
inline int32_t OptionsRepeatDirection(std::array<int32_t,2>& counters,
    std::span<const int32_t> intervals,const std::array<bool,2>& pressed,const std::array<bool,2>& held){
  const auto action=[&](size_t i){
    auto& counter=counters[i];
    if(pressed[i]){counter=0;return true;}
    if(!held[i]){counter=0;return false;}
    counter=std::bit_cast<int32_t>(uint32_t(counter)+1);
    return OptionsRepeatDeadline(counter,intervals);
  };
  if(action(0))return 1;if(action(1))return -1;return 0;
}
}
