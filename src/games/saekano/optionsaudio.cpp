#include "optionsaudio.h"
#include "optionsrepeat.h"
#include "commands.h"
#include <limits>
namespace Impacto::Saekano {
void StartOptionsAudio(OptionsAudioState& s,const OptionsEnvironment& e,const OptionsValues& values,std::span<const int32_t> repeats){
  ValidateOptionsRepeatIntervals(repeats);s={};s.RepeatIntervals.assign(repeats.begin(),repeats.end());
  const auto count=e.Scalars.at(0x18);if(count==0 || count>11)throw FormatError("invalid Options audio range");s.Maximum=int32_t(count)-1;
  for(size_t i=0;i<5;++i){
    const auto at=uint16_t(0x1f0+i*16);const auto origin=e.Points.at(at),step=e.Points.at(uint16_t(at+8));
    if(values[7+i]>=count || step[0]<=0 || int64_t(origin[0])+int64_t(step[0])*11>std::numeric_limits<int32_t>::max())throw FormatError("invalid Options audio selector");
    s.Layouts[i]={OptionsSliderKind::Audio,int32_t(i),origin[0],origin[1],step[0],e.Rectangles.at(0x494)[2]};s.Sliders[i].Value=values[7+i];s.Sliders[i].X=int32_t(int64_t(origin[0])+int64_t(step[0])*values[7+i]);
  }
  s.Started=true;
}
int32_t UpdateOptionsAudio(OptionsAudioState& s,OptionsRowNavigation& n,OptionsValues& values,const OptionsAudioInput& in,int32_t mode,OptionsAudioServices& h){
  if(!s.Started)throw UnsupportedCommand("Options audio page has no owner");
  if(h.UpdateNavigation(n))return 0;
  if(n.Minimum!=0 || n.Maximum!=5 || n.Selected<0 || n.Selected>5 || n.Visible<=0 || n.Visible>6 || n.Margin<0 || n.Margin>=n.Visible || n.Scroll<0 || n.Scroll>5)throw FormatError("invalid Options audio row navigator");
  const auto snap=[&](size_t i){s.Sliders[i].X=int32_t(int64_t(s.Layouts[i].OriginX)+int64_t(s.Layouts[i].Step)*values[7+i]);};
  const auto select=[&](int32_t row){if(n.Selected!=row){h.UiSound(2);SelectOptionsRow(n,row);}};
  const auto toggle=[&]{values[12]=uint8_t(values[12]==0);h.SetToggleVariant(values[12]==0);};
  bool available=true;
  for(size_t i=0;i<5 && available;++i){h.BeginSliderWidget(uint32_t(i));s.Sliders[i].KeyboardActive=true;UpdateOptionsSlider(s.Sliders[i],n,values,s.Layouts[i],in.Sliders[i],h);available=s.Sliders[i].KeyboardActive;}
  if(available){h.UpdateBackgroundWidget();if(in.ReleasedBackground)for(size_t i=0;i<5;++i)snap(i);}
  h.UpdateToggleWidget();if(available && in.ReleasedToggle){toggle();select(5);available=false;}
  for(size_t i=0;i<6;++i){h.UpdateLabelWidget(uint32_t(i));if(available && in.ReleasedLabels[i]){select(int32_t(i));available=false;}}
  if(!available)return 1;
  const auto row=size_t(n.Selected);
  if(row<5){
    const auto direction=OptionsRepeatDirection(s.RepeatCounters,s.RepeatIntervals,in.Pressed,in.Held);
    if(direction){h.UiSound(2);auto& v=s.Sliders[row].Value;v=std::clamp(v+direction,0,s.Maximum);}
    else if(row==3 && in.PreviewPressed){
      if(mode<0 || mode>4)throw FormatError("Options preview mode outside native banks");
      if(mode!=4){h.PrepareSystemPreview(7,mode);h.StartSystemPreview(7);}
    }
    values[7+row]=uint8_t(s.Sliders[row].Value);ApplyOptionsVolume(int32_t(row),int32_t(values[7+row])*10,h);snap(row);
  }else if(in.Pressed[0] || in.Pressed[1]){h.UiSound(2);toggle();}
  return 0;
}
}
