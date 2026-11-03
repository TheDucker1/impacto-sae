#include "extrasbattle.h"
#include "commands.h"
#include <cmath>

namespace Impacto::Saekano {
namespace {
UiSpriteDefinition Hit(std::array<int32_t,4> r){UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;}
bool Released(const BattleCardSprite& w){return w.Input.Enabled && (w.Input.Flags&8)!=0;}
}
ExtrasBattleBrowser::ExtrasBattleBrowser(const ExtrasScreenEnvironment& e,SemanticState& owner,
    ExtrasBattleServices& services,std::span<const int32_t> repeats,uint32_t hold,bool all,uint64_t now)
    :env_(e),owner_(owner),services_(services),back_(Hit(e.Common.CancelRectangle),{},1,true,hold),
     flick_(Hit(e.Rectangles.at(0xdbe4)),{},1,true,hold){
  if(!owner.SystemProgress || !owner.BattleHistory.AllKnown() || e.BattleReplays.empty() ||
      e.BattleReplays.size()>1000000 || e.BattleRectangles.size()!=4 || e.Common.FadeDuration<0)
    throw FormatError("battle gallery requires authored entries, four rows and known system history");
  if(all)for(size_t i=0;i<e.BattleReplays.size();++i)MarkMusicPlayed(owner.BattleHistory,i);
  for(const auto& r:e.BattleRectangles)rows_.emplace_back(Hit(r),ProfileTextureKey{},1,true,hold);
  SlotBrowserSettings settings;settings.Pitch=e.Points.at(0xdbf4)[1];
  settings.ScrollFrames=std::bit_cast<int32_t>(e.Scalars.at(0xdbfc));
  settings.PageStep=4;settings.PageFrames=settings.ScrollFrames;
  StartListBrowser(list_,settings,repeats,int32_t(e.BattleReplays.size()),4,0,0,now);
  fade_.Configure(255,0,uint32_t(e.Common.FadeDuration),false,now);
}
int32_t ExtrasBattleBrowser::Status(size_t row)const{return ExactMusicStatus(owner_.BattleHistory,row);}
bool ExtrasBattleBrowser::Unlocked(size_t row)const{const auto v=Status(row);return v==1 || v==2 || v==3;}
size_t ExtrasBattleBrowser::UnlockedCount()const{size_t n=0;for(size_t i=0;i<env_.BattleReplays.size();++i)n+=Unlocked(i);return n;}
void ExtrasBattleBrowser::Sample(BattleCardSprite& w,uint64_t now){
  w.Update({0,0},SampleWidgetContact(w.HitRectangle({0,0}),contacts_.Current[0],contacts_.Previous[0],w.Input.TrackedId,contacts_.Velocity(0)),now);
  contacts_.Publish(w.Input.Flags);
}
void ExtrasBattleBrowser::CancelPointer(){
  contacts_.Current=contacts_.Previous={};
  for(auto* w:{&back_,&flick_}){w->Input.Flags=0;w->Input.TrackedId=-1;}
  for(auto& w:rows_){w.Input.Flags=0;w.Input.TrackedId=-1;}
}
void ExtrasBattleBrowser::Confirm(){if(Unlocked(size_t(Selected()))){services_.BeginConfirmation();pending_=6;}}
void ExtrasBattleBrowser::ResumeReplay(uint64_t now){
  if(!ReplayRequested())throw FormatError("battle gallery has no suspended replay");
  CancelPointer();returnPhase_=5;fade_.Configure(255,0,uint32_t(env_.Common.FadeDuration),true,now);phase_=4;
}
int32_t ExtrasBattleBrowser::Update(const ExtrasBattleInput& in,uint64_t now){
  if(finished_)return 1;
  if(pending_){phase_=*pending_;pending_.reset();}
  // Match immediate setup/resource transitions and deferred fade destinations.
  if(phase_==0){arrows_.Begin(std::bit_cast<float>(env_.Scalars.at(0xde58)),std::bit_cast<int32_t>(env_.Scalars.at(0xde5c)),now);returnPhase_=5;phase_=1;}
  if(phase_==1)phase_=2;
  if(phase_==2){if(services_.ResourcesReady())pending_=3;}
  else if(phase_==3){fade_.Configure(255,0,uint32_t(env_.Common.FadeDuration),true,now);phase_=4;}
  if(phase_==4){fade_.Tick(now);if(fade_.Finished())pending_=returnPhase_;}
  else if(phase_==5){
    const auto sound=[&](int32_t id){services_.UiSound(id);};UpdateListNavigation(list_,in.Navigation,sound);
    if(!list_.Scrolling){
      Sample(flick_,now);Sample(back_,now);bool flickHandled=false,rowReleased=false;
      if(flick_.Input.Enabled && (flick_.Input.Flags&0x80)!=0){
        if(!flick_.Input.AbsoluteVelocity || !std::isfinite(flick_.Input.Velocity[1]))throw FormatError("battle gallery flick velocity unavailable");
        PageListBrowser(list_,flick_.Input.Velocity[1]<0,sound);flickHandled=true;
      }
      for(size_t i=0;i<rows_.size();++i){
        Sample(rows_[i],now);
        if(!flickHandled && Released(rows_[i])){
          const auto row=list_.Rows.Scroll+int32_t(i);
          if(row<=list_.Rows.Maximum){SelectListBrowserRow(list_,row);Confirm();}rowReleased=true;break;
        }
      }
      if(!rowReleased){
        if(in.Accept)Confirm();
        else if(in.Cancel || Released(back_)){services_.UiSound(4);returnPhase_=17;pending_=15;}
      }
      const auto row=size_t(Selected());
      if(Status(row)==1){auto& h=owner_.BattleHistory;if(row>=h.Entries.size())h.Entries.resize(row+1,h.Default);h.Entries[row]=int32_t(3);dirty_=true;}
    }
  }else if(phase_==6){
    const auto result=services_.PollConfirmation();
    if(!result.Busy){
      if(!result.Cancelled && result.Selection==1){services_.StopMusic();returnPhase_=7;pending_=15;}
      else pending_=5;
    }
  }else if(phase_==15){fade_.Reverse(now);pending_=16;}
  else if(phase_==16){fade_.Tick(now);if(fade_.Finished())pending_=returnPhase_;}
  else if(phase_==17){phase_=18;finished_=true;return 1;}
  arrows_.Update(now);return 0;
}
}
