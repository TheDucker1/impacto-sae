#include "optionstext.h"
#include "commands.h"
#include "optionsrepeat.h"
#include <algorithm>
#include <bit>
#include <limits>
namespace Impacto::Saekano {
namespace {
constexpr std::array<size_t,3> Numeric{0,1,3};
constexpr std::array<size_t,4> Toggles{2,4,5,6};
struct SliderSink final:OptionsSliderServices {
  OptionsTextServices& Host;uint32_t Index;
  SliderSink(OptionsTextServices& host,uint32_t index):Host(host),Index(index){}
  void UpdateWidget(int32_t x,int32_t y)override{Host.UpdateSliderWidget(Index,x,y);}
  void UiSound(int32_t id)override{Host.UiSound(id);}
  void SetBgmVolume(int32_t)override{throw FormatError("text slider requested audio output");}
  void SetSeVolume(int32_t)override{throw FormatError("text slider requested audio output");}
  void SetVoiceVolume(int32_t)override{throw FormatError("text slider requested audio output");}
  void SetChannelVolume(uint32_t,int32_t)override{throw FormatError("text slider requested audio output");}
  void SetUiVolume(int32_t)override{throw FormatError("text slider requested audio output");}
};
}
void StartOptionsText(OptionsTextState& s,const OptionsEnvironment& e,const OptionsValues& values,std::span<const int32_t> repeats){
  ValidateOptionsRepeatIntervals(repeats);
  s={};s.RepeatIntervals.assign(repeats.begin(),repeats.end());
  for(size_t i=0;i<3;++i){
    const auto at=uint16_t(0x16c+i*16);const auto origin=e.Points.at(at),step=e.Points.at(uint16_t(at+8));
    const auto count=e.Scalars.at(i==2?0x18:0x14);
    if(count==0 || count>256 || values[Numeric[i]]>=count || step[0]<=0 ||
        int64_t(origin[0])+int64_t(step[0])*std::max(int32_t(count)-1,i==2?11:5)>std::numeric_limits<int32_t>::max())throw FormatError("invalid Options text selector");
    s.Maximum[i]=int32_t(count)-1;s.Layouts[i]={OptionsSliderKind::Text,int32_t(i),origin[0],origin[1],step[0],e.Rectangles.at(0x494)[2]};
    s.Sliders[i].Value=values[Numeric[i]];s.Sliders[i].X=int32_t(int64_t(origin[0])+int64_t(step[0])*values[Numeric[i]]);
  }
  s.Started=true;
}
int32_t UpdateOptionsText(OptionsTextState& s,OptionsRowNavigation& n,OptionsValues& values,const OptionsTextInput& in,OptionsTextServices& h){
  if(!s.Started)throw UnsupportedCommand("Options text page has no owner");
  if(h.UpdateNavigation(n))return 0;
  if(n.Minimum!=0 || n.Maximum!=6 || n.Selected<0 || n.Selected>6 || n.Visible<=0 || n.Visible>7 || n.Margin<0 || n.Margin>=n.Visible || n.Scroll<0 || n.Scroll>6)throw FormatError("invalid Options text row navigator");
  const auto snap=[&](size_t i){s.Sliders[i].X=int32_t(int64_t(s.Layouts[i].OriginX)+int64_t(s.Layouts[i].Step)*values[Numeric[i]]);};
  const auto select=[&](int32_t row){if(n.Selected!=row){h.UiSound(2);SelectOptionsRow(n,row);}};
  const auto toggle=[&](size_t i){auto& v=values[Toggles[i]];v=uint8_t(v==0);h.SetToggleVariant(uint32_t(i),v==0);};
  bool available=true;
  for(size_t i=0;i<3 && available;++i){
    SliderSink sink(h,uint32_t(i));s.Sliders[i].KeyboardActive=true;
    UpdateOptionsSlider(s.Sliders[i],n,values,s.Layouts[i],in.Sliders[i],sink);available=s.Sliders[i].KeyboardActive;
  }
  if(available){h.UpdateBackgroundWidget();if(in.ReleasedBackground)for(size_t i=0;i<3;++i)snap(i);}
  for(size_t i=0;i<4;++i){h.UpdateToggleWidget(uint32_t(i));if(available && in.ReleasedToggles[i]){toggle(i);select(int32_t(i)+3);available=false;}}
  for(size_t i=0;i<7;++i){h.UpdateLabelWidget(uint32_t(i));if(available && in.ReleasedLabels[i]){select(int32_t(i));available=false;}}
  if(!available)return 1;
  const auto row=size_t(n.Selected);
  if(row<3){
    const auto direction=OptionsRepeatDirection(s.RepeatCounters,s.RepeatIntervals,in.Pressed,in.Held);
    if(direction){h.UiSound(2);auto& v=s.Sliders[row].Value;v=std::clamp(v+direction,0,s.Maximum[row]);}
    values[Numeric[row]]=uint8_t(s.Sliders[row].Value);snap(row);
  }else if(in.Pressed[0] || in.Pressed[1]){h.UiSound(2);toggle(row-3);}
  return 0;
}
}
