#include "optionsnavigation.h"
#include "optionsrepeat.h"
#include "commands.h"
#include <algorithm>
namespace Impacto::Saekano {
namespace {
void Check(const OptionsRowNavigation& n){
  // Backlog restores its authored three-row viewport after selecting within a
  // shorter initial count; blank leading rows can give it a negative scroll.
  if(n.Minimum!=0 || n.Maximum<0 || n.Maximum>1000000 || n.Selected<0 || n.Selected>n.Maximum || n.Visible<=0 || n.Visible>1000001 ||
      n.Scroll<std::min(0,n.Maximum-n.Visible+1) || n.Scroll>n.Maximum || n.Margin<0 || n.Margin>=n.Visible)
    throw FormatError("invalid Options vertical navigator");
}
}
void StartOptionsNavigation(OptionsNavigationState& s,OptionsRowNavigation& n,uint32_t count,uint32_t visible,uint32_t margin,std::span<const int32_t> repeats,bool continuous){
  ValidateOptionsRepeatIntervals(repeats);
  if(count==0 || count>1000001 || visible==0 || visible>count || margin>=visible)throw FormatError("invalid Options navigation extent");
  s={};s.Started=true;s.Continuous=continuous;s.RepeatIntervals.assign(repeats.begin(),repeats.end());
  n={};n.Maximum=int32_t(count)-1;n.Visible=int32_t(visible);n.Margin=int32_t(margin);
}
bool UpdateOptionsNavigation(OptionsNavigationState& s,OptionsRowNavigation& n,const OptionsNavigationInput& in){
  if(!s.Started)throw UnsupportedCommand("Options navigator has no owner");Check(n);
  s.Activated={};s.Any=false;s.Sounds=0;
  if(s.Enabled)for(size_t i=0;i<2;++i){
    auto& counter=s.Counters[i];bool active=false;
    if(counter!=-1){
      if(in.Pressed[i]){counter=0;active=true;}
      else if(!in.Held[i])counter=0;
      else {counter=std::bit_cast<int32_t>(uint32_t(counter)+1);active=OptionsRepeatDeadline(counter,s.RepeatIntervals);}
    }
    if(active){
      const auto oldSelected=n.Selected,oldScroll=n.Scroll;
      if(i==0){if(n.Selected!=0)SelectOptionsRow(n,n.Selected-1);else if(s.Wrap)SelectOptionsRow(n,n.Maximum);}
      else {if(n.Selected!=n.Maximum)SelectOptionsRow(n,n.Selected+1);else if(s.Wrap)SelectOptionsRow(n,0);}
      // Keyboard navigation records the old position; pointer focus records
      // the new position in SelectOptionsRow. Preserve the caller distinction.
      n.PreviousSelected=oldSelected;n.PreviousScroll=oldScroll;
      if(!s.Continuous && n.Selected==(i==0?0:n.Maximum))counter=-1;
      if(s.SoundEnabled && oldSelected!=n.Selected)++s.Sounds;
      s.Activated[i]=true;s.Any=true;
    }
  }
  for(size_t i=0;i<2;++i)if(s.Counters[i]==-1 && !in.Held[i])s.Counters[i]=0;
  return s.Any;
}
void StartOptionsPageNavigators(std::array<OptionsNavigationState,3>& states,std::array<OptionsRowNavigation,3>& rows,const OptionsEnvironment& e,std::span<const int32_t> repeats){
  const std::array<size_t,3> counts{e.PointTables.at(0x15c).size(),e.PointTables.at(0x1e0).size(),e.SpriteTables.at(0x240).size()};
  for(size_t i=0;i<3;++i){if(counts[i]<2 || counts[i]>1000001)throw FormatError("invalid authored Options row count");StartOptionsNavigation(states[i],rows[i],uint32_t(counts[i]),uint32_t(counts[i]-1),0,repeats);}
}
}
