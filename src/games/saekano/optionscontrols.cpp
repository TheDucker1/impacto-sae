#include "optionscontrols.h"
#include "commands.h"
#include <bit>
#include <cmath>
#include <limits>
namespace Impacto::Saekano {
void StartOptionsControls(OptionsControlState& s,const OptionsEnvironment& e,bool unlocked,uint64_t now){
  const auto duration=e.Scalars.at(0x4a4);const auto travel=std::bit_cast<float>(e.Scalars.at(0x4a8));
  if(duration>uint32_t(std::numeric_limits<int32_t>::max()) || !std::isfinite(travel))throw FormatError("invalid options tab motion");
  s={};s.Started=true;s.TabCount=unlocked?4:3;s.SwitchDuration=duration;s.ArrowTravel=travel;
  // Runtime initializer80059bb4, not the zero-filled BSS image.
  s.ArrowCurve={0.25f,0.1f,0.25f,1.0f};
  s.SampleProgress.Configure(0,0,0,true,now);
  for(size_t i=0;i<2;++i){s.ArrowProgress[i].Configure(0,0,0,true,now);s.Arrows[i].Configure(0,travel,int32_t(duration)/2,s.ArrowCurve,false,now);}
}
int32_t UpdateOptionsControls(OptionsControlState& s,const OptionsControlInput& in,OptionsControlServices& h,uint64_t now){
  if(!s.Started || s.TabCount<3 || s.TabCount>4 || s.SelectedTab<0 || s.SelectedTab>=s.TabCount)
    throw FormatError("invalid options tab owner");
  bool contact=false;
  for(int32_t i=0;i<s.TabCount;++i){
    h.UpdateTabContact(uint32_t(i));
    if(in.ReleasedTabs[size_t(i)]){if(i!=s.SelectedTab){h.UiSound(11);s.SelectedTab=i;}contact=true;}
  }
  if(!contact){
    const auto consumed=s.SelectedTab<3?h.UpdatePage(uint32_t(s.SelectedTab)):0;
    if(!consumed){
      if(in.Close){h.UiSound(3);h.StopChannel(8);return 1;}
      if(in.Revert || h.RevertReleased().value_or(in.ReleasedRevert)){h.UiSound(4);h.StopChannel(8);return 2;}
      if(in.PreviousTab || in.NextTab){
        const size_t direction=in.PreviousTab?0:1;
        s.SelectedTab=(s.SelectedTab+s.TabCount+(direction==0?-1:1))%s.TabCount;
        s.SampleProgress.Configure(0,0,0,true,now);h.UiSound(11);s.ArrowActive[direction]=true;
        s.Arrows[direction].Configure(0,s.ArrowTravel,int32_t(s.SwitchDuration)/2,s.ArrowCurve,true,now);
        s.ArrowProgress[direction].Configure(0,100,s.SwitchDuration,true,now);
      }
    }
  }
  if(s.SampleNavigationEnabled){
    const auto selected=h.UpdateSampleNavigation(s.SampleSelected);
    if(selected!=s.SampleSelected){s.SampleSelected=selected;h.ResetSamplePage();}
  }
  for(size_t i=0;i<2;++i){
    s.ArrowProgress[i].Tick(now);s.Arrows[i].Tick(now);
    if(s.ArrowActive[i]){
      if(s.ArrowProgress[i].Finished())s.ArrowActive[i]=false;
      else if(s.Arrows[i].Current==s.Arrows[i].To){
        auto& m=s.Arrows[i];std::swap(m.From,m.To);m.Current=m.From;m.Epoch=now;m.Elapsed=0;
      }
    }
  }
  h.UpdateRevertContact();return 0;
}
}
