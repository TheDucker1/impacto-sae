#include "extrasmusic.h"
#include "commands.h"
#include <cmath>

namespace Impacto::Saekano {
namespace {
UiSpriteDefinition Hit(std::array<int32_t,4> r){UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;}
bool Released(const BattleCardSprite& w){return w.Input.Enabled && (w.Input.Flags&8)!=0;}
}
ExtrasMusicBrowser::ExtrasMusicBrowser(const ExtrasScreenEnvironment& e,SemanticState& owner,
    ExtrasMusicServices& services,std::span<const int32_t> repeats,uint32_t hold,bool all,uint64_t now)
    :env_(e),owner_(owner),services_(services),
     play_(e.SpriteTables.at(0xd648).at(0),{ProfileTextureSection::SystemUi,-1,32},1,true,hold),
     stop_(e.SpriteTables.at(0xd658).at(0),{ProfileTextureSection::SystemUi,-1,37},1,true,hold),
     modeButton_(Hit(e.Rectangles.at(0xd678)),{},1,true,hold),
     back_(Hit(e.Common.CancelRectangle),{},1,true,hold),
     flick_(Hit(e.Rectangles.at(0xd688)),{},1,true,hold){
  if(!owner.SystemProgress || !owner.MusicHistory.AllKnown() || e.MusicTracks.empty() ||
      e.MusicTracks.size()>1000000 || e.AlbumRectangles.size()!=6 || e.Common.FadeDuration<0)
    throw FormatError("music gallery requires authored tracks, six rows and known system history");
  for(const auto& t:e.MusicTracks){
    if(t.Wave<0 || t.History<0 || t.History>1000000 || t.DurationSeconds<=0 || t.DurationSeconds>INT32_MAX/1000)
      throw FormatError("invalid gallery music identity/duration");
    if(all)MarkMusicPlayed(owner.MusicHistory,size_t(t.History));
  }
  for(const auto& r:e.AlbumRectangles)rows_.emplace_back(Hit(r),ProfileTextureKey{},1.0f,true,hold);
  SlotBrowserSettings settings;settings.Pitch=e.Points.at(0xd698)[1];
  settings.ScrollFrames=std::bit_cast<int32_t>(e.Scalars.at(0xd6a0));
  settings.PageStep=6;settings.PageFrames=settings.ScrollFrames;
  settings.HighlightTo=e.Common.SharedWords[1];settings.HighlightDuration=e.Common.SharedWords[0];
  StartListBrowser(list_,settings,repeats,int32_t(e.MusicTracks.size()),6,0,0,now);
  fade_.Configure(255,0,uint32_t(e.Common.FadeDuration),false,now);
  services_.StopMusic();
}
int32_t ExtrasMusicBrowser::Status(size_t row)const{return ExactMusicStatus(owner_.MusicHistory,size_t(env_.MusicTracks.at(row).History));}
bool ExtrasMusicBrowser::Unlocked(size_t row)const{const auto v=Status(row);return v==1 || v==2 || v==3;}
size_t ExtrasMusicBrowser::UnlockedCount()const{size_t count=0;for(size_t i=0;i<env_.MusicTracks.size();++i)count+=Unlocked(i);return count;}
void ExtrasMusicBrowser::Sample(BattleCardSprite& w,uint64_t now){
  w.Update({0,0},SampleWidgetContact(w.HitRectangle({0,0}),contacts_.Current[0],contacts_.Previous[0],w.Input.TrackedId,contacts_.Velocity(0)),now);
  contacts_.Publish(w.Input.Flags);
}
void ExtrasMusicBrowser::CancelPointer(){
  contacts_.Current=contacts_.Previous={};
  for(auto* w:{&play_,&stop_,&modeButton_,&back_,&flick_}){w->Input.Flags=0;w->Input.TrackedId=-1;}
  for(auto& w:rows_){w.Input.Flags=0;w.Input.TrackedId=-1;}
}
void ExtrasMusicBrowser::BuildPlaylist(int32_t first){
  playlist_.clear();playlistIndex_=0;
  const auto count=int32_t(env_.MusicTracks.size());
  for(int32_t i=0;i<count;++i){const int32_t row=(first+i)%count;if(Unlocked(size_t(row)))playlist_.push_back(row);}
}
void ExtrasMusicBrowser::Play(int32_t row){
  if(!Unlocked(size_t(row))){services_.UiSound(5);return;}
  const auto& t=env_.MusicTracks.at(size_t(row));
  services_.PlayMusic(t.Wave);playing_=row;
  const auto id=size_t(t.History);
  if(Status(size_t(row))==1){
    auto& h=owner_.MusicHistory;if(id>=h.Entries.size())h.Entries.resize(id+1,h.Default);
    h.Entries[id]=int32_t(3);dirty_=true;
  }
  auto& played=owner_.SystemProgress->Histories[2];
  const auto v=id<played.Values.size()?played.Values[id]:played.Fallback;
  if(v!=1 && v!=2 && v!=3){
    if(id>=played.Values.size())played.Values.resize(id+1,played.Fallback);
    played.Values[id]=1;dirty_=true;
  }
}
void ExtrasMusicBrowser::Activate(){
  if(!Unlocked(size_t(Selected())))return;
  if(playing_==Selected()){
    playIcon_=services_.Paused();services_.SetPaused(!playIcon_);
  }else {BuildPlaylist(Selected());Play(Selected());playIcon_=true;}
}
int32_t ExtrasMusicBrowser::Update(const ExtrasMusicInput& in,uint64_t now){
  if(finished_)return 1;
  if(pending_){phase_=*pending_;pending_.reset();}
  if(phase_==0)phase_=1;
  if(phase_==1){fade_.Restart(now);phase_=2;}
  if(phase_==2){fade_.Tick(now);if(fade_.Finished())phase_=3;}
  if(phase_==3){
    const auto sound=[&](int32_t id){services_.UiSound(id);};
    UpdateListNavigation(list_,in.Navigation,sound);
    if(!list_.Scrolling){
      Sample(flick_,now);bool handled=false;
      if(flick_.Input.Enabled && (flick_.Input.Flags&0x80)!=0){
        if(!flick_.Input.AbsoluteVelocity || !std::isfinite(flick_.Input.Velocity[1]))throw FormatError("music flick velocity unavailable");
        PageListBrowser(list_,flick_.Input.Velocity[1]<0,sound);handled=true;
      }
      for(size_t i=0;i<rows_.size();++i){
        Sample(rows_[i],now);
        if(!handled && Released(rows_[i])){
          const auto row=list_.Rows.Scroll+int32_t(i);
          if(row<=list_.Rows.Maximum){SelectListBrowserRow(list_,row);Activate();}
          handled=true;
        }
      }
      // Native control widgets publish their current contact flags after the
      // action dispatch; actions here consume the preceding sample.
      if(!handled){
        if(in.Accept || Released(play_))Activate();
        else if(in.Cancel || Released(back_)){services_.UiSound(4);pending_=4;}
        else if(in.Stop || Released(stop_)){
          services_.StopMusic();playlist_.clear();playlistIndex_=0;playing_=-1;playIcon_=false;
        }else if(in.Mode || Released(modeButton_))mode_=(mode_+1)%3;
      }
    }
    for(auto* w:{&play_,&stop_,&modeButton_,&back_})Sample(*w,now);
    list_.Highlight.Tick(now);
  }
  if(phase_==4){fade_.Reverse(now);phase_=5;}
  if(phase_==5){fade_.Tick(now);if(fade_.Finished())phase_=6;}
  if(phase_==6)phase_=7;
  if(phase_==7){services_.RestoreExtrasMusic();finished_=true;return 1;}
  if(playing_>=0 && !services_.Busy()){
    if(mode_==1 && !playlist_.empty()){
      if(++playlistIndex_==playlist_.size()){const auto first=playlist_.front();BuildPlaylist(first);}
      if(!playlist_.empty())Play(playlist_.at(playlistIndex_));
    }else if(mode_==2)Play(playing_);
    else {playlist_.clear();playlistIndex_=0;playing_=-1;playIcon_=false;}
  }
  return 0;
}
}
