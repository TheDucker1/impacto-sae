#include "slotbrowser.h"
#include "commands.h"
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
int32_t Wrap(uint32_t v){return std::bit_cast<int32_t>(v);}
void StartScroll(SlotBrowserState& s,int32_t frames){
  const auto& n=s.Rows;s.Scrolling=true;
  s.Scroll.Start(0,Wrap(uint32_t(n.Scroll-n.PreviousScroll)*uint32_t(s.Settings.Pitch)),frames);
  s.Cursor.Start(0,Wrap(uint32_t((n.PreviousSelected-n.PreviousScroll)-(n.Selected-n.Scroll))*uint32_t(s.Settings.Pitch)),frames);
}
}
void PageListBrowser(SlotBrowserState& s,bool forward,const std::function<void(int32_t)>& sound){
  auto& n=s.Rows;const auto oldSelected=n.Selected,oldScroll=n.Scroll;
  s.Navigation.Any=true;
  s.Navigation.Activated[forward?0:1]=true;s.PageActivated[forward?1:0]=true;
  if(forward){if(n.Selected==n.Maximum){if(s.Navigation.Wrap)SelectOptionsRow(n,0);else s.Navigation.Any=false;}
    else SelectOptionsRow(n,n.Selected+s.Settings.PageStep);
  }else{if(n.Selected==0){if(s.Navigation.Wrap)SelectOptionsRow(n,n.Maximum);else s.Navigation.Any=false;}
    else SelectOptionsRow(n,n.Selected-s.Settings.PageStep);
  }
  n.PreviousSelected=oldSelected;n.PreviousScroll=oldScroll;s.PageCounters[forward?1:0]=-1;
  if(n.Selected!=oldSelected && s.Navigation.SoundEnabled)sound(2);
  if(s.Navigation.Any)StartScroll(s,s.Settings.PageFrames);
}
void SlotFrameChannel::Start(int32_t from,int32_t to,int32_t frames){
  if(frames<0)throw FormatError("negative slot scroll frame count");Current=From=from;To=to;Frames=frames?frames:1;Elapsed=0;
}
void SlotFrameChannel::Tick(){
  if(Elapsed<Frames){
    const auto delta=uint32_t(To)-uint32_t(From);const auto product=Wrap(delta*(uint32_t(Elapsed)+1));++Elapsed;
    Current=Wrap(uint32_t(From)+uint32_t(int64_t(product)/Frames));
  }else Current=To;
}
void SelectSlotBrowserRow(SlotBrowserState& s,int32_t selected){
  if(!s.Started || selected<0 || selected>=60)throw FormatError("invalid slot browser selection");
  SelectListBrowserRow(s,selected);
}
void SelectListBrowserRow(SlotBrowserState& s,int32_t selected){
  if(!s.Started || selected<0 || selected>s.Rows.Maximum)throw FormatError("invalid list selection");
  s.Scroll.Start(0,0,0);s.Cursor.Start(0,0,0);SelectOptionsRow(s.Rows,selected);
}
void StartSlotBrowser(SlotBrowserState& s,const SlotBrowserSettings& p,std::span<const int32_t> repeats,int32_t selected,uint64_t now){
  if(p.Pitch<=0 || p.Pitch>1000000 || p.ScrollFrames<0 || p.PageFrames<0 || p.PageStep<=0 || p.PageStep>60 || p.HighlightDuration<0)throw FormatError("invalid slot browser settings");
  StartListBrowser(s,p,repeats,60,4,0,selected,now);
}
void StartListBrowser(SlotBrowserState& s,const SlotBrowserSettings& p,std::span<const int32_t> repeats,
    int32_t count,int32_t visible,int32_t margin,int32_t selected,uint64_t now){
  if(p.Pitch<=0 || p.Pitch>1000000 || p.ScrollFrames<0 || p.PageFrames<0 || p.PageStep<=0 || p.PageStep>1000000 || p.HighlightDuration<0)
    throw FormatError("invalid shared list settings");
  s={};s.Settings=p;StartOptionsNavigation(s.Navigation,s.Rows,uint32_t(count),uint32_t(visible),uint32_t(margin),repeats);
  s.Started=true;SelectListBrowserRow(s,selected);
  s.Highlight.Configure(255,p.HighlightTo,p.HighlightDuration,true,now,-1);
}
void UpdateListNavigation(SlotBrowserState& s,const OptionsNavigationInput& in,const std::function<void(int32_t)>& sound){
  if(!s.Started)throw UnsupportedCommand("list has no owner");
  if(s.RenderEnabled){
    bool busy=false;if(s.Scrolling){if(s.Scroll.Elapsed==s.Scroll.Frames)s.Scrolling=false;else busy=true;}
    if(!busy && s.KeyboardEnabled){
      s.PageActivated={};UpdateOptionsNavigation(s.Navigation,s.Rows,in);
      for(uint32_t i=0;i<s.Navigation.Sounds;++i)sound(2);
      if(s.Navigation.Any)StartScroll(s,s.Settings.ScrollFrames);
    }
    s.Scroll.Tick();s.Cursor.Tick();
  }
}
int32_t UpdateSlotBrowser(SlotBrowserState& s,const SlotBrowserInput& in,SlotBrowserServices& h,std::span<const uint8_t,60> occupied,int32_t mode,uint64_t now){
  UpdateListNavigation(s,in.Vertical,[&](int32_t id){h.UiSound(id);});
  int32_t result=3;bool handled=false;
  if(!s.Navigation.Any){
    h.UpdateWidget(SlotBrowserWidget::Page,0);
    if(h.PageFlick()){
      const auto velocity=h.PageVelocityY();if(!std::isfinite(velocity) || std::abs(double(velocity))>=2147483648.0)throw FormatError("invalid slot page velocity");
      handled=true;PageListBrowser(s,int32_t(velocity)<0,[&](int32_t id){h.UiSound(id);});
    }
    for(uint32_t i=0;i<4;++i){
      h.UpdateWidget(SlotBrowserWidget::Protect,i);
      if(!handled && h.Released(SlotBrowserWidget::Protect,i)){SelectSlotBrowserRow(s,s.Rows.Scroll+int32_t(i));result=8;handled=true;}
    }
    for(uint32_t i=0;i<4;++i){
      h.UpdateWidget(SlotBrowserWidget::Activate,i);
      if(result==3 && !handled && h.Released(SlotBrowserWidget::Activate,i)){
        SelectSlotBrowserRow(s,s.Rows.Scroll+int32_t(i));
        if(!occupied[size_t(s.Rows.Selected)] && mode==1)h.UiSound(2);
        result=4;handled=true;
      }
    }
    h.UpdateWidget(SlotBrowserWidget::Back,0);
    if(!handled){if(in.Protect)result=8;else if(in.Accept)result=4;else if(in.Cancel || h.Released(SlotBrowserWidget::Back,0)){h.UiSound(4);result=5;}}
  }
  s.Highlight.Tick(now);return result;
}
}
