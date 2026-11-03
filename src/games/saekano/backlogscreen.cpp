#include "backlogscreen.h"
#include "commands.h"
#include <algorithm>
#include <cmath>

namespace Impacto::Saekano {
BacklogEnvironment BacklogEnvironment::Decode(std::span<const uint8_t> bytes){
  BacklogEnvironment e;PayloadReader r(bytes);for(auto& f:e.Flags)f=r.U8();
  const auto rgba=[&]{const auto v=r.U32();return Color{uint8_t(v>>16),uint8_t(v>>8),uint8_t(v),uint8_t(v>>24)};};
  const auto point=[&](uint16_t at){e.Points[at]={r.I32(),r.I32()};};
  const auto word=[&](uint16_t at){e.Scalars[at]=r.U32();};
  const auto color=[&](uint16_t at){e.Colors[at]=rgba();};
  const auto sprite=[&](uint16_t at){e.Sprites.emplace(at,UiSpriteDefinition::Read(r));};
  const auto rect=[&]{return Rectangle{r.I32(),r.I32(),r.I32(),r.I32()};};
  point(0xbe08);word(0xbe10);point(0xbe14);color(0xbe1c);color(0xbe20);color(0xbe24);
  sprite(0xbe28);e.Rectangles[0xbe4c]=rect();sprite(0xbe5c);sprite(0xbe80);sprite(0xbea4);
  point(0xbec8);sprite(0xbed0);
  for(uint16_t at=0xbef4;at<=0xbf14;at+=8)point(at);
  word(0xbf1c);auto n=r.U32();r.CheckCount(n,16);for(uint32_t i=0;i<n;++i)e.RowRectangles.push_back(rect());
  sprite(0xbf30);sprite(0xbf54);point(0xbf78);
  for(uint16_t at=0xbf80;at<=0xbfec;at+=36)sprite(at);
  word(0xc010);word(0xc014);sprite(0xc018);sprite(0xc03c);point(0xc060);
  for(uint16_t at=0xc068;at<=0xc0d4;at+=36)sprite(at);
  point(0xc0f8);point(0xc100);
  n=r.U32();r.CheckCount(n,36);for(uint32_t i=0;i<n;++i)e.VoiceIcons.push_back(UiSpriteDefinition::Read(r));
  word(0xc118);sprite(0xc11c);sprite(0xc140);e.Rectangles[0xc164]=rect();
  for(uint16_t at=0xc174;at<=0xc1a0;at+=4)word(at);
  color(0xc1a4);color(0xc1a8);color(0xc1ac);
  n=r.U32();r.CheckCount(n,4);for(uint32_t i=0;i<n;++i)e.Unused.push_back(r.I32());
  e.RewindQuestion=r.String();n=r.U32();r.CheckCount(n,4);
  for(uint32_t i=0;i<n;++i)e.Text.Colors.push_back(rgba());
  e.Text.LayoutParameter=e.Scalars.at(0xbf1c);e.Text.Scale=std::bit_cast<float>(e.Scalars.at(0xc180));
  if(r.Remaining() || !std::isfinite(e.Text.Scale) || e.Text.Scale<=0 || e.RowRectangles.size()<3 ||
      e.VoiceIcons.size()<2 || e.Word(0xc19c)<=0 || e.Word(0xc188)<=0 || e.Word(0xc188)>3)
    throw FormatError("invalid backlog environment: remaining="+std::to_string(r.Remaining())+
      " rows="+std::to_string(e.RowRectangles.size())+" icons="+std::to_string(e.VoiceIcons.size())+
      " scale="+std::to_string(e.Text.Scale)+" fade="+std::to_string(e.Word(0xc19c))+
      " visible="+std::to_string(e.Word(0xc188)));
  return e;
}
namespace {
UiSpriteDefinition White(UiSpriteDefinition s){s.Color={255,255,255,255};return s;}
UiSpriteDefinition Contact(std::array<int32_t,4> r){UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;}
ProfileTextureKey Main(uint32_t i){return {ProfileTextureSection::MainUi,-1,i};}
}
BacklogScreen::BacklogScreen(BacklogEnvironment e,size_t count,std::span<const int32_t> repeats,uint32_t hold,BacklogScreenServices& services)
    :env_(std::move(e)),count_(count),services_(services),
     back_(White(env_.Sprites.at(0xc140)),{ProfileTextureSection::SharedUi,-1,2},1,false,hold),
     backContact_(Contact(env_.Rectangles.at(0xc164)),Main(0),1,true,hold){
  if(count>BacklogHistory::Capacity)throw FormatError("backlog screen exceeds retained ring");
  for(size_t i=0;i<3;++i)rows_.emplace_back(Contact(env_.RowRectangles.at(i)),Main(0),1,true,hold);
  for(auto at:{0xbf80,0xbfa4,0xbfc8,0xbfec})arrows_.emplace_back(White(env_.Sprites.at(uint16_t(at))),Main(38),1,true,hold);
  if(count){
    const auto visible=std::min(env_.Word(0xc188),int32_t(count));
    StartListBrowser(list_,{env_.Points.at(0xc100)[1],env_.Word(0xc184),env_.Word(0xc190),env_.Word(0xc194),0,0},
        repeats,int32_t(count),visible,env_.Word(0xc18c),int32_t(count)-1,0);
    // Setup selects with the short count, then restores the authored page span.
    list_.Rows.Visible=env_.Word(0xc188);
  }else {list_.Rows.Selected=-1;list_.Rows.Maximum=-1;}
  rewindFade_.Configure(0,0,0,false,0);
  icon_.Configure(0,2,env_.Word(0xc118),false,0,-1,false);
}
void BacklogScreen::CancelPointer(){
  contacts_.Current=contacts_.Previous={};
  for(auto* group:{&rows_,&arrows_})for(auto& body:*group){body.Input.Flags=0;body.Input.TrackedId=-1;}
  backContact_.Input.Flags=0;backContact_.Input.TrackedId=-1;
}
void BacklogScreen::Sample(BattleCardSprite& b,std::array<int32_t,2> offset,uint64_t now){
  b.Update(offset,SampleWidgetContact(b.HitRectangle(offset),contacts_.Current[0],contacts_.Previous[0],
      b.Input.TrackedId,contacts_.Velocity(0)),now);contacts_.Publish(b.Input.Flags);
}
int32_t BacklogScreen::Update(const BacklogInput& in,uint64_t now){
  if(pendingPhase_){phase_=*pendingPhase_;pendingPhase_.reset();}
  const auto sound=[&](int32_t id){services_.UiSound(id);};
  const auto toggle=[&](size_t row){if(!services_.VoicePlaying(row))icon_.Restart(now);services_.ToggleVoice(row);};
  if(phase_==0){fade_.Configure(0,env_.Word(0xc19c),uint32_t(env_.Word(0xc198)),true,now);phase_=1;}
  if(phase_==1){fade_.Tick(now);if(fade_.Finished()){
    if(!arrow_.Started)arrow_.Begin(std::bit_cast<float>(env_.Scalars.at(0xc010)),env_.Word(0xc014),now);
    pendingPhase_=2;
  }}
  if(phase_==2){
    bool handled=false;
    if(count_ && !list_.Scrolling)for(size_t i=0;i<3;++i){
      Sample(rows_[i],{0,0},now);const auto row=list_.Rows.Scroll+int32_t(i);
      if(!handled && row>=0 && row<int32_t(count_)){
        if(rows_[i].Input.Flags&4){SelectListBrowserRow(list_,row);pendingPhase_=3;handled=true;}
        else if(rows_[i].Input.Flags&0x80){PageListBrowser(list_,rows_[i].Input.Velocity[1]<0,sound);handled=true;}
        else if(rows_[i].Input.Flags&8){SelectListBrowserRow(list_,row);toggle(size_t(row));handled=true;}
      }
    }
    for(size_t i=0;i<4;++i)Sample(arrows_[i],{0,(i<2?-1:1)*int32_t(arrow_.Current)},now);
    Sample(backContact_,{0,0},now);
    if(!handled && (!count_ || !list_.Scrolling)){
      if(in.Cancel){
        if(count_ && services_.VoicePlaying(size_t(Selected())))services_.ToggleVoice(size_t(Selected()));
        else{services_.UiSound(4);fade_.Reverse(now);pendingPhase_=5;}
      }else if(count_ && in.Replay)toggle(size_t(Selected()));
      else if(count_ && in.Rewind)pendingPhase_=3;
      else if(backContact_.Input.Flags&8){services_.UiSound(4);fade_.Reverse(now);pendingPhase_=5;}
    }
    // Native4bef0 samples both arrows, tests the second pressed result, and
    // lets20b40 tick scrolling/keyboard state last when contact did not win.
    if(count_ && !handled && !(arrows_[2].Input.Flags&1))UpdateListNavigation(list_,in.Vertical,sound);
    icon_.Tick(now);
  }
  if(phase_==3){services_.BeginRewindQuestion(env_.RewindQuestion);pendingPhase_=4;}
  if(phase_==4){
    const auto q=services_.PollRewindQuestion();
    if(!q.Busy){
      if(!q.Cancelled && q.Selection==1){rewindFade_.Configure(0,255,uint32_t(env_.Word(0xc1a0)),true,now);pendingPhase_=6;}
      else{services_.RetireRewindQuestion();pendingPhase_=2;}
    }
  }
  if(phase_==5){fade_.Tick(now);if(fade_.Finished())return 1;}
  if(phase_==6){rewindFade_.Tick(now);if(rewindFade_.Finished())return 2;}
  // Own initializer4cf94..4cfb8 configures the same(.25,.1,.25,1) curve as
  // ProfileArrowMotion;9ecd6 reverses at the endpoint rather than restarting0.
  if(arrow_.Started)arrow_.Update(now);
  return 0;
}
SlotViewportInputs BacklogScreen::Viewport()const{
  SlotViewportInputs in;in.Rows=list_.Rows;in.Rectangle=env_.Rectangles.at(0xbe4c);
  in.Pitch=env_.Points.at(0xc100)[1];in.ScrollOffset=list_.Scroll.Current;in.CursorOffset=list_.Cursor.Current;
  in.Scrolling=list_.Scrolling;in.Visible=count_!=0;return in;
}
std::vector<UiSpriteBlit> BacklogScreen::Widgets()const{
  std::vector<UiSpriteBlit> out{back_.Draw({255,255,255,255})};
  for(size_t i:{size_t(0),size_t(2)}){
    auto s=arrows_[i+(arrows_[i].Input.Flags&1?1:0)].Shape;
    s.Y+=(i<2?-1:1)*int32_t(arrow_.Current);
    out.push_back(ResolveUiSprite(Main(38),s,1,i<2?1.f:-1.f));
  }
  return out;
}
}
