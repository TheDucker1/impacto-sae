#include "extrasmovie.h"
#include "commands.h"
#include <cmath>

namespace Impacto::Saekano {
namespace {
UiSpriteDefinition Rectangle(std::array<int32_t,4> r){UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;}
int32_t Word(const ExtrasScreenEnvironment& e,uint16_t key){return std::bit_cast<int32_t>(e.Scalars.at(key));}
int32_t History(const SystemSaveHistory& h,size_t i){return i<h.Values.size()?h.Values[i]:h.Fallback;}
void Store(SystemSaveHistory& h,size_t i,int32_t v){if(i>=h.Values.size())h.Values.resize(i+1,h.Fallback);h.Values[i]=v;}
}
ExtrasMovieBrowser::ExtrasMovieBrowser(const ExtrasScreenEnvironment& e,SemanticState& owner,
    ExtrasMovieServices& services,std::span<const int32_t> repeats,uint32_t hold,bool all,uint64_t now)
    :env_(e),owner_(owner),services_(services),
     back_([&]{auto s=e.Common.SharedSprites[5];s.Color={255,255,255,255};return s;}(),{ProfileTextureSection::SharedUi,-1,2},1,true,hold),
     play_(Rectangle(e.Rectangles.at(0xd984)),{},1,true,hold),
     flick_(Rectangle(e.Rectangles.at(0xd968)),{},1,true,hold),
     left_(Rectangle({e.Sprites.at(0xd920).X,e.Sprites.at(0xd920).Y,e.Sprites.at(0xd920).Width,e.Sprites.at(0xd920).Height}),{},1,true,hold),
     right_(Rectangle({e.Sprites.at(0xd944).X,e.Sprites.at(0xd944).Y,e.Sprites.at(0xd944).Width,e.Sprites.at(0xd944).Height}),{},1,true,hold){
  if(!owner_.SystemProgress || e.Movies.empty() || e.Movies.size()>owner_.Bank1.size())
    throw FormatError("movie gallery requires complete authored data and system owner");
  if(e.Common.FadeDuration<0)throw FormatError("negative movie gallery fade duration");
  for(const auto key:{0xd880,0xd884,0xd888,0xd980,0xdad0})if(Word(e,uint16_t(key))<0)throw FormatError("negative movie gallery timing");
  for(size_t i=0;i<e.Movies.size();++i)if(!Unlocked(i) && (all || owner_.Bank1[i]!=0))
    Store(owner_.SystemProgress->Histories[4],i,1);
  SlotBrowserSettings settings;settings.Pitch=e.Points.at(0xd978)[0];settings.ScrollFrames=Word(e,0xd980);
  settings.PageStep=1;settings.PageFrames=settings.ScrollFrames;
  StartListBrowser(list_,settings,repeats,int32_t(e.Movies.size()),1,0,0,now);
  arrows_.Begin(std::bit_cast<float>(e.Scalars.at(0xdacc)),Word(e,0xdad0),now);
  delay_.Configure(0,100,uint32_t(Word(e,0xd888)),false,now);
  fade_.Configure(255,0,uint32_t(e.Common.FadeDuration),false,now);
  const auto& s=e.Sprites.at(0xd9e8);
  movieRect_[2].Configure(s.Width,0,0,true,now);movieRect_[3].Configure(s.Height,0,0,true,now);
}
int32_t ExtrasMovieBrowser::Status(size_t i)const{return History(owner_.SystemProgress->Histories[4],i);}
bool ExtrasMovieBrowser::Unlocked(size_t i)const{const auto v=Status(i);return v==1 || v==2 || v==3;}
bool ExtrasMovieBrowser::Sample(BattleCardSprite& widget,std::array<int32_t,2> offset,uint64_t now){
  widget.Update(offset,SampleWidgetContact(widget.HitRectangle(offset),contacts_.Current[0],contacts_.Previous[0],widget.Input.TrackedId,contacts_.Velocity(0)),now);
  contacts_.Publish(widget.Input.Flags);return widget.Input.Enabled && (widget.Input.Flags&8)!=0;
}
void ExtrasMovieBrowser::CancelPointer(){
  contacts_.Current=contacts_.Previous={};
  for(auto* w:{&back_,&play_,&flick_,&left_,&right_}){w->Input.Flags=0;w->Input.TrackedId=-1;}
}
void ExtrasMovieBrowser::Changed(uint64_t now){
  if(services_.MovieBusy() && !services_.MoviePaused())services_.PauseMovie();
  changed_=true;delay_.Configure(0,100,uint32_t(Word(env_,0xd888)),true,now);
}
void ExtrasMovieBrowser::OpenMovie(uint64_t now){
  const auto& s=env_.Sprites.at(0xd9e8);
  const std::array<int32_t,4> from{std::bit_cast<int32_t>(uint32_t(s.X)+uint32_t(Word(env_,0xd9a4))),s.Y,s.Width,s.Height};
  constexpr std::array<int32_t,4> to{0,0,960,544};
  for(size_t i=0;i<4;++i)movieRect_[i].Configure(from[i],to[i],uint32_t(Word(env_,0xd884)),true,now);
  browsing_=false;play_.Shape=Rectangle(env_.Rectangles.at(0xd994));play_.Input.Flags=0;play_.Input.TrackedId=-1;
  const auto selected=size_t(Selected());
  if(Status(selected)==1){Store(owner_.SystemProgress->Histories[4],selected,3);dirty_=true;}
  auto& played=owner_.SystemProgress->Histories[5];const auto value=History(played,selected);
  if(value!=1 && value!=2 && value!=3){Store(played,selected,1);dirty_=true;}
  if(!services_.MovieBusy() && services_.MovieIdle()){
    services_.PrepareMovie(selected);preview_=Selected();changed_=false;cycle_=2;
  }
  phase_=6;
}
void ExtrasMovieBrowser::TickRectangle(uint64_t now){for(auto& channel:movieRect_)channel.Tick(now);}
std::array<int32_t,4> ExtrasMovieBrowser::MovieRectangle()const{
  return {movieRect_[0].Value.Current,movieRect_[1].Value.Current,movieRect_[2].Value.Current,movieRect_[3].Value.Current};
}
int32_t ExtrasMovieBrowser::Update(const ExtrasMovieInput& input,uint64_t now){
  if(finished_)return 1;
  if(pending_){phase_=*pending_;pending_.reset();}
  if(phase_==0){arrows_.Begin(std::bit_cast<float>(env_.Scalars.at(0xdacc)),Word(env_,0xdad0),now);phase_=1;}
  if(phase_==1){
    const auto& s=env_.Sprites.at(0xd9e8);
    movieRect_[2].Configure(s.Width,0,0,true,now);movieRect_[3].Configure(s.Height,0,0,true,now);
    services_.PrepareMovie(0);previewHidden_=true;preview_=0;phase_=2;
  }
  if(phase_==2 && services_.ResourcesReady() && services_.MovieReady())phase_=3;
  if(phase_==3){fade_.Restart(now);phase_=4;}
  if(phase_==4){fade_.Tick(now);if(fade_.Finished()){
    services_.StartMovie();previewHidden_=false;phase_=5;
  }}
  if(phase_==5){
    UpdateListNavigation(list_,input.Horizontal,[&](int32_t id){services_.UiSound(id);});
    if(list_.Scrolling)Changed(now);
    else {
      const bool back=Sample(back_,{0,0},now),play=Sample(play_,{0,0},now);
      Sample(flick_,{0,0},now);
      if(flick_.Input.Enabled && (flick_.Input.Flags&0x80)!=0){
        if(!flick_.Input.AbsoluteVelocity)throw FormatError("movie flick velocity is unavailable");
        const auto velocity=flick_.Input.Velocity[0];if(!std::isfinite(velocity))throw FormatError("invalid movie flick velocity");
        PageListBrowser(list_,velocity<=0,[&](int32_t id){services_.UiSound(id);});
      }else if(play && Unlocked(size_t(Selected())))OpenMovie(now);
      else {
        const bool left=Sample(left_,{-int32_t(arrows_.Current),0},now);
        const bool right=Sample(right_,{int32_t(arrows_.Current),0},now);
        if((Selected()!=list_.Rows.Minimum && left) || (Selected()!=list_.Rows.Maximum && right)){
          PageListBrowser(list_,!left,[&](int32_t id){services_.UiSound(id);});Changed(now);
        }else if(input.Accept && Unlocked(size_t(Selected())))OpenMovie(now);
        else if(input.Cancel || back){services_.UiSound(4);phase_=10;}
      }
      if(changed_){
        preview_=-1;
        if(!Unlocked(size_t(Selected())))changed_=false;
        else if(services_.MovieBusy())services_.StopMovie();
        else if(services_.MovieIdle()){
          delay_.Tick(now);if(delay_.Finished()){
            services_.PrepareMovie(size_t(Selected()));preview_=Selected();cycle_=1;changed_=false;
          }
        }
      }
    }
  }
  if(phase_==6){TickRectangle(now);if(movieRect_[0].Finished())pending_=7;}
  if(phase_==7){
    if(services_.MovieBusy()){
      Sample(play_,{0,0},now);
      if(play_.Pressed() || input.Accept){
        for(auto& channel:movieRect_)channel.Reverse(now);play_.Shape=Rectangle(env_.Rectangles.at(0xd984));phase_=9;
      }
    }
    if(!services_.MovieBusy())phase_=8;
  }
  if(phase_==8){for(auto& channel:movieRect_)channel.Reverse(now);play_.Shape=Rectangle(env_.Rectangles.at(0xd984));phase_=9;}
  if(phase_==9){TickRectangle(now);if(movieRect_[0].Finished()){browsing_=true;phase_=5;}}
  if(phase_==10){fade_.Reverse(now);phase_=11;}
  if(phase_==11){fade_.Tick(now);if(fade_.Finished()){
    services_.StopMovie();if(services_.MovieIdle())phase_=12;
  }}
  if(phase_==12)phase_=13;
  if(phase_==13){services_.RestoreExtrasMusic();finished_=true;return 1;}
  arrows_.Update(now);
  if(browsing_){
    if(cycle_==0){services_.ReprepareMovie();previewHidden_=true;cycle_=1;}
    if(cycle_==1 && services_.MovieReady() && !list_.Scrolling){
      services_.StartMovie();previewHidden_=false;list_.KeyboardEnabled=true;cycle_=2;
    }
    if(cycle_==2 && services_.MoviePositionUs()>=uint64_t(env_.Scalars.at(0xd880))*1000000){
      services_.StopMovie();cycle_=0;
    }
  }else if(phase_==7 && services_.MovieReady())services_.StartMovie();
  return 0;
}
}
