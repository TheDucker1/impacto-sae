#include "optionsvoice.h"
#include "commands.h"
#include <bit>
#include <limits>
namespace Impacto::Saekano {
void StartOptionsVoice(OptionsVoiceState& s,const OptionsEnvironment& e,const OptionsValues& values){
  if(e.Scalars.at(0xc)!=5 || e.Scalars.at(0x10)!=9 || values[13]>=5 || e.Scalars.at(0x32c)>uint32_t(std::numeric_limits<int32_t>::max()))throw FormatError("invalid Options voice configuration");
  s={};s.Mode=values[13];s.HighlightDuration=e.Scalars.at(0x32c);s.HighlightTarget=std::bit_cast<int32_t>(e.Scalars.at(0x328));s.Started=true;
}
int32_t UpdateOptionsVoice(OptionsVoiceState& s,OptionsRowNavigation& n,TitleTween& highlight,OptionsValues& values,const OptionsVoiceInput& in,OptionsVoiceServices& h,uint64_t now){
  if(!s.Started || s.Mode<0 || s.Mode>4 || s.Character<0 || s.Character>8 || n.Minimum!=0 || n.Maximum!=1 || n.Selected<0 || n.Selected>1 || n.Scroll<0 || n.Scroll>1 || n.Visible<=0 || n.Visible>2 || n.Margin<0 || n.Margin>=n.Visible)throw FormatError("invalid Options voice owner");
  const auto animate=[&]{highlight.Configure(0,s.HighlightTarget,s.HighlightDuration,true,now);};
  const auto select=[&](int32_t group){if(n.Selected!=group){h.UiSound(2);SelectOptionsRow(n,group);}};
  const auto mode=[&]{if(s.Mode==4)h.StopChannel(7);else {h.PrepareSystemPreview(7,s.Mode);h.StartSystemPreview(7);}animate();values[13]=uint8_t(s.Mode);};
  const auto toggle=[&]{auto& v=values[14+size_t(s.Character)];v=uint8_t(v==0);h.SetCharacterVariant(uint32_t(s.Character),v==0);};
  bool available=true;
  for(size_t i=0;i<5;++i){h.UpdateModeWidget(uint32_t(i));if(available && in.ReleasedModes[i]){s.Mode=int32_t(i);mode();available=false;select(0);}}
  for(size_t i=0;i<9;++i){h.UpdateCharacterToggle(uint32_t(i));if(available && in.ReleasedToggles[i]){s.Character=int32_t(i);toggle();available=false;select(1);}}
  for(size_t i=0;i<9;++i){h.UpdateCharacterLabel(uint32_t(i));if(available && in.ReleasedCharacterLabels[i]){s.Character=int32_t(i);available=false;select(1);}}
  for(size_t i=0;i<2;++i){h.UpdateGroupLabel(uint32_t(i));if(available && in.ReleasedGroupLabels[i]){select(int32_t(i));available=false;}}
  highlight.Tick(now);if(highlight.Finished())highlight.Configure(0,0,0,true,now);
  if(!available)return 1;
  if(n.Selected==1){
    if(in.Pressed[0]){h.UiSound(2);if(s.Character>=3)s.Character-=3;else {animate();SelectOptionsRow(n,0);}}
    else if(in.Pressed[1]){h.UiSound(2);if(s.Character<=5)s.Character+=3;else {animate();SelectOptionsRow(n,0);}}
    else if(in.Pressed[2]){h.UiSound(2);s.Character=(s.Character+1)%9;}
    else if(in.Pressed[3]){h.UiSound(2);s.Character=(s.Character+8)%9;}
    else if(in.Pressed[4]){h.UiSound(2);toggle();}
  }else if(!h.UpdateNavigation(n)){
    if(in.Pressed[2]){s.Mode=(s.Mode+1)%5;mode();}
    else if(in.Pressed[3]){s.Mode=(s.Mode+4)%5;mode();}
  }
  return 0;
}
}
