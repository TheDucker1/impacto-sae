#include "optionsslider.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <limits>
namespace Impacto::Saekano {
namespace {
void SelectRow(OptionsRowNavigation& n,int32_t row){
  const auto delta=row-n.Selected;n.Selected=row;
  if(delta>0){
    if(row<n.Maximum-n.Margin){if(row>=n.Scroll+n.Visible-n.Margin)n.Scroll=row-(n.Visible-n.Margin)+1;}
    else {n.Selected=std::min(row,n.Maximum);n.Scroll=n.Maximum-n.Visible+1;}
  }else if(delta<0){
    if(row<n.Minimum+n.Margin){n.Selected=std::max(row,n.Minimum);n.Scroll=n.Minimum;}
    else if(row<n.Scroll+n.Margin)n.Scroll=row-n.Margin;
  }
  n.PreviousSelected=n.Selected;n.PreviousScroll=n.Scroll;
}
}
int32_t UpdateOptionsSlider(OptionsSliderState& s,OptionsRowNavigation& n,OptionsValues& values,
    const OptionsSliderLayout& l,const OptionsSliderInput& in,OptionsSliderServices& h){
  const bool text=l.Kind==OptionsSliderKind::Text;
  if(l.Kind!=OptionsSliderKind::Text && l.Kind!=OptionsSliderKind::Audio)throw FormatError("invalid options slider kind");
  const int32_t maximum=text && l.Row!=2?4:10;
  if(l.Row<0 || l.Row>=(text?3:5) || l.Step<=0 || l.Width<0 ||
      int64_t(l.OriginX)-l.Step/2<std::numeric_limits<int32_t>::min() ||
      int64_t(l.OriginX)+int64_t(l.Step)*(maximum+1)>std::numeric_limits<int32_t>::max())
    throw FormatError("invalid options slider track");
  // Authored row navigators have small, nonnegative extents. Validate before
  // performing the native signed scroll arithmetic rather than copying UB.
  if(n.Minimum<0 || n.Maximum<n.Minimum || n.Maximum>1000000 || n.Selected<n.Minimum || n.Selected>n.Maximum ||
      n.Scroll<n.Minimum || n.Scroll>n.Maximum || n.Visible<=0 || n.Visible>n.Maximum-n.Minimum+1 ||
      n.Margin<0 || n.Margin>=n.Visible || l.Row<n.Minimum || l.Row>n.Maximum)
    throw FormatError("invalid options slider row navigator");
  const size_t index=text?(l.Row==2?3:size_t(l.Row)):size_t(7+l.Row);
  if(values[index]>maximum)throw FormatError("options slider value exceeds authored range");
  const auto snap=[&]{s.X=l.OriginX+l.Step*values[index];};
  const auto quantize=[&]{
    const int64_t shifted=int64_t(s.X)-l.OriginX+l.Step/2;
    return int32_t(std::clamp(shifted<=0?int64_t(0):shifted/l.Step,int64_t(0),int64_t(maximum)));
  };
  const auto commit=[&]{
    s.Value=quantize();values[index]=uint8_t(s.Value);
    if(text)snap();
    else {
      const int32_t percent=s.Value*10;
      switch(l.Row){
        case 0:h.SetBgmVolume(percent);break;
        case 1:h.SetSeVolume(percent);break;
        case 2:h.SetVoiceVolume(percent);break;
        case 3:for(const auto channel:{7u,8u,11u})h.SetChannelVolume(channel,percent);break;
        case 4:h.SetUiVolume(percent);break;
      }
    }
  };
  h.UpdateWidget(s.X,l.Y);
  if(in.Pressed){snap();if(n.Selected!=l.Row){SelectRow(n,l.Row);h.UiSound(2);}s.KeyboardActive=false;}
  if(in.Held){
    const int32_t x=std::bit_cast<int32_t>(uint32_t(in.PointerX)-uint32_t(l.Width/2));
    s.X=std::clamp(x,l.OriginX,l.OriginX+l.Step*maximum);
    if(!text || s.X!=x)commit();
    s.KeyboardActive=false;
  }
  if(in.Released){commit();s.KeyboardActive=false;return 0;}
  return text?0:l.OriginX;
}
}
