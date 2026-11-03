#include "extrascg.h"
#include "commands.h"
#include "optionsrepeat.h"
#include "transition.h"
#include <algorithm>
#include <cmath>

namespace Impacto::Saekano {
namespace {
UiSpriteDefinition Hit(std::array<int32_t,4> r){UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;}
int32_t Word(const ExtrasScreenEnvironment& e,uint16_t key){return std::bit_cast<int32_t>(e.Scalars.at(key));}
bool IsUnlocked(int32_t value){return value==1 || value==2 || value==3;}
}
ExtrasCgBrowser::ExtrasCgBrowser(const ExtrasScreenEnvironment& e,AlbumGalleryCatalogue catalogue,
    SemanticState& owner,ExtrasCgServices& services,std::span<const int32_t> repeats,
    uint32_t hold,bool unlockAll,uint64_t now)
    :env_(e),catalogue_(std::move(catalogue)),owner_(owner),services_(services),
     back_(e.Common.SharedSprites[5],{ProfileTextureSection::SharedUi,-1,2},1,true,hold),
     flick_(Hit(e.Rectangles.at(0xd2f8)),{},1,true,hold),
     viewer_(Hit({0,0,960,544}),{},1,true,hold),repeats_(repeats.begin(),repeats.end()){
  if(!owner_.SystemProgress || !owner_.CgHistory.Known || owner_.CgHistory.Partial || catalogue_.Cards.empty())
    throw UnsupportedCommand("CG gallery requires a complete history owner and catalogue");
  if(e.Common.FadeDuration<0 || Word(e,0xd478)<0)throw FormatError("negative CG gallery duration");
  if(unlockAll)for(const auto& entry:catalogue_.Entries)for(const auto image:entry.Images){
    if(!Unlocked(image)){
      auto& h=owner_.CgHistory;if(size_t(image)>=h.Values.size())h.Values.resize(size_t(image)+1,h.Default);
      h.Values[size_t(image)]=1;
    }
  }
  StartOptionsNavigation(categoryNavigation_,categories_,6,6,0,repeats);
  categoryNavigation_.SoundEnabled=false;
  SlotBrowserSettings p;p.Pitch=e.Points.at(0xd308)[0];p.ScrollFrames=Word(e,0xd6a0);
  p.PageStep=1;p.PageFrames=p.ScrollFrames;
  StartListBrowser(list_,p,repeats,int32_t(catalogue_.PageCardCounts.size()),1,0,0,now);
  const auto tab=e.Rectangles.at(0xd23c);
  for(int32_t i=0;i<6;++i){auto r=tab;r[1]+=i*r[3];categoryWidgets_.emplace_back(Hit(r),ProfileTextureKey{},1.0f,true,hold);}
  const auto& origin=e.Points.at(0xd314);const auto& pitch=e.Points.at(0xd340);const auto& face=e.Sprites.at(0xd31c);
  for(int32_t i=0;i<9;++i)cells_.emplace_back(Hit({origin[0]+i%3*pitch[0],origin[1]+i/3*pitch[1],face.Width,face.Height}),ProfileTextureKey{},1.0f,true,hold);
  fade_.Configure(255,0,uint32_t(e.Common.FadeDuration),false,now);
  imageFade_.Configure(0,255,uint32_t(Word(e,0xd478)),false,now);
  previousFade_.Configure(255,0,uint32_t(Word(e,0xd478)),false,now);
}
bool ExtrasCgBrowser::Unlocked(int32_t image)const{return IsUnlocked(std::get<int32_t>(CgHistoryStatus(owner_.CgHistory,image)));}
std::vector<bool> ExtrasCgBrowser::Variants(size_t card)const{
  std::vector<bool> result;if(const auto* entry=catalogue_.Card(card))for(const auto id:entry->Images)result.push_back(Unlocked(id));return result;
}
bool ExtrasCgBrowser::New(size_t card)const{
  if(const auto* entry=catalogue_.Card(card))for(const auto image:entry->Images)
    if(std::get<int32_t>(CgHistoryStatus(owner_.CgHistory,image))==1)return true;
  return false;
}
size_t ExtrasCgBrowser::TotalImages()const{size_t count=0;for(const auto& e:catalogue_.Entries)count+=e.Images.size();return count;}
size_t ExtrasCgBrowser::UnlockedImages()const{size_t count=0;for(const auto& e:catalogue_.Entries)for(const auto id:e.Images)if(Unlocked(id))++count;return count;}
bool ExtrasCgBrowser::Sample(BattleCardSprite& widget,std::array<int32_t,2> offset,uint64_t now){
  widget.Update(offset,SampleWidgetContact(widget.HitRectangle(offset),contacts_.Current[0],contacts_.Previous[0],widget.Input.TrackedId,contacts_.Velocity(0)),now);
  contacts_.Publish(widget.Input.Flags);return widget.Input.Enabled && (widget.Input.Flags&8)!=0;
}
void ExtrasCgBrowser::CancelPointer(){
  contacts_.Current=contacts_.Previous={};
  const auto clear=[](BattleCardSprite& w){w.Input.Flags=0;w.Input.TrackedId=-1;};
  clear(back_);clear(flick_);clear(viewer_);for(auto& w:categoryWidgets_)clear(w);for(auto& w:cells_)clear(w);
}
void ExtrasCgBrowser::SelectPage(int32_t page){SelectListBrowserRow(list_,page);cell_=0;}
void ExtrasCgBrowser::SyncCategory(){
  int32_t category=0;
  while(category<5 && list_.Rows.Selected>catalogue_.CategoryLastPage[size_t(category)])++category;
  SelectOptionsRow(categories_,category);
}
void ExtrasCgBrowser::MoveCell(int32_t delta){
  const auto count=catalogue_.PageCardCounts.at(size_t(list_.Rows.Selected));
  if(count<=0)throw FormatError("CG page has no cards");
  cell_=(cell_+delta%count+count)%count;
}
void ExtrasCgBrowser::Open(uint64_t now){
  imageFade_.Configure(0,255,uint32_t(Word(env_,0xd478)),false,now);
  variants_=Variants(size_t(SelectedCard()));
  const auto first=std::find(variants_.begin(),variants_.end(),true);
  if(first==variants_.end()){services_.UiSound(5);pending_=5;return;}
  variant_=int32_t(first-variants_.begin());services_.UiSound(3);pending_=7;
}
void ExtrasCgBrowser::StopComment(){if(audioPhase_!=0 && audioPhase_!=6)audioReturn_=6;}
void ExtrasCgBrowser::MoveEntry(bool forward,bool wrappedVariant){
  const auto original=SelectedCard();int32_t card=original;
  for(size_t i=0;i<catalogue_.Cards.size();++i){
    card=(card+(forward?1:int32_t(catalogue_.Cards.size())-1))%int32_t(catalogue_.Cards.size());
    const auto mask=Variants(size_t(card));
    if(std::find(mask.begin(),mask.end(),true)==mask.end())continue;
    if(card!=original){
      SelectPage(card/9);cell_=card%9;SyncCategory();variants_=mask;
      variant_=int32_t(std::find(mask.begin(),mask.end(),true)-mask.begin());
      services_.UiSound(11);pending_=7;
    }else if(wrappedVariant){services_.UiSound(11);pending_=7;}
    break;
  }
  StopComment();
}
void ExtrasCgBrowser::UpdateComment(){
  if(audioPending_){audioPhase_=*audioPending_;audioPending_.reset();}
  if(audioPhase_==1){audioReturn_=5;audioPhase_=2;}
  if(audioPhase_==2){
    services_.StopComment();
    if(services_.CommentIdle()){
      const auto* entry=catalogue_.Card(size_t(SelectedCard()));
      if(!entry)throw FormatError("comment selected an empty CG card");
      services_.PrepareComment(entry->Gates[0]);audioPhase_=3;
    }
  }
  if(audioPhase_==3){services_.StartComment();audioPhase_=4;}
  if(audioPhase_==4 && services_.CommentPlaying()){
    auto& h=owner_.SystemProgress->Histories[7];const auto card=size_t(SelectedCard());
    const auto value=card<h.Values.size()?h.Values[card]:h.Fallback;
    if(!IsUnlocked(value)){if(card>=h.Values.size())h.Values.resize(card+1,h.Fallback);h.Values[card]=1;}
    audioPhase_=5;
  }
  if(audioPhase_==5){audioPhase_=audioReturn_;if(!services_.CommentPlaying() || services_.CommentIdle())audioPhase_=6;}
  if(audioPhase_==6){services_.StopComment();audioPending_=7;return;}
  if(audioPhase_==7){services_.StopComment();if(services_.CommentIdle())audioPending_=0;}
}
int32_t ExtrasCgBrowser::Update(const ExtrasCgInput& input,uint64_t now){
  if(finished_)return 1;
  if(pending_){phase_=*pending_;pending_.reset();}
  const auto sound=[&](int32_t id){services_.UiSound(id);};
  if(phase_==0)phase_=1;
  if(phase_==1 && services_.ResourcesReady())phase_=2;
  if(phase_==2){fade_.Restart(now);phase_=3;}
  if(phase_==3){fade_.Tick(now);if(fade_.Finished())phase_=4;}
  if(phase_==4){
    if(UpdateOptionsNavigation(categoryNavigation_,categories_,input.Categories)){
      SelectPage(Category()==0?0:catalogue_.CategoryLastPage[size_t(Category()-1)]+1);
    }else {
      bool handled=false;
      for(size_t i=0;i<categoryWidgets_.size();++i)if(Sample(categoryWidgets_[i],{int32_t(i)==Category()?Word(env_,0xd238):0,0},now) && !handled){
        SelectOptionsRow(categories_,int32_t(i));SelectPage(i==0?0:catalogue_.CategoryLastPage[i-1]+1);
        sound(3);pending_=5;handled=true;
      }
      for(int32_t i=0;i<catalogue_.PageCardCounts[size_t(list_.Rows.Selected)];++i)if(Sample(cells_[size_t(i)],{0,0},now) && !handled){cell_=i;sound(3);phase_=6;handled=true;}
      if(!handled){if(input.Accept){sound(3);pending_=5;}else if(input.Cancel || Sample(back_,{0,0},now)){sound(4);phase_=11;}}
    }
  }
  if(phase_==5){
    UpdateListNavigation(list_,input.Pages,sound);
    if(list_.Navigation.Any){cell_=0;SyncCategory();}
    else {
      bool handled=false;Sample(flick_,{0,0},now);
      if(flick_.Input.Enabled && (flick_.Input.Flags&0x80)){
        if(!flick_.Input.AbsoluteVelocity || !std::isfinite(flick_.Input.Velocity[0]))throw FormatError("CG gallery flick velocity unavailable");
        PageListBrowser(list_,flick_.Input.Velocity[0]<=0,sound);cell_=0;SyncCategory();handled=true;
      }
      for(int32_t i=0;i<catalogue_.PageCardCounts[size_t(list_.Rows.Selected)];++i)if(Sample(cells_[size_t(i)],{0,0},now) && !handled){cell_=i;sound(3);phase_=6;handled=true;}
      for(size_t i=0;i<categoryWidgets_.size();++i)if(Sample(categoryWidgets_[i],{int32_t(i)==Category()?Word(env_,0xd238):0,0},now) && !handled){
        if(int32_t(i)!=Category()){SelectOptionsRow(categories_,int32_t(i));SelectPage(i==0?0:catalogue_.CategoryLastPage[i-1]+1);sound(3);}handled=true;
      }
      if(!handled){
        if(input.Grid[0]){MoveCell(-3);sound(2);}else if(input.Grid[1]){MoveCell(3);sound(2);}
        else if(input.Grid[2]){MoveCell(-1);sound(2);}else if(input.Grid[3]){MoveCell(1);sound(2);}
        else if(input.Cancel){sound(4);phase_=4;}else if(Sample(back_,{0,0},now)){sound(4);phase_=11;}
        else if(input.Accept){sound(3);phase_=6;}
      }
    }
  }
  if(phase_==6)Open(now);
  if(phase_==7){
    const auto* entry=catalogue_.Card(size_t(SelectedCard()));
    if(!entry)throw FormatError("CG viewer selected an empty card");
    services_.PrepareImage(entry->Images.at(size_t(variant_)),size_t(nextBuffer_));
    previousBuffer_=currentBuffer_;currentBuffer_=-1;
    previousFade_.Configure(255,0,uint32_t(Word(env_,0xd478)),false,now);pending_=8;
  }
  if(phase_==8 && services_.ImageReady(size_t(nextBuffer_))){
    currentBuffer_=nextBuffer_;nextBuffer_^=1;pending_=9;viewerVisible_=true;
    imageFade_.Enable(now);previousFade_.Restart(now);
    const auto image=catalogue_.Card(size_t(SelectedCard()))->Images.at(size_t(variant_));
    if(std::get<int32_t>(CgHistoryStatus(owner_.CgHistory,image))==1){
      auto& h=owner_.CgHistory;if(size_t(image)>=h.Values.size())h.Values.resize(size_t(image)+1,h.Default);
      h.Values[size_t(image)]=3;dirty_=true;
    }
  }
  if(phase_==9){
    imageFade_.Tick(now);previousFade_.Tick(now);
    if(imageFade_.Finished() && previousFade_.Finished()){
      if(input.Comment){if(audioPhase_==0){sound(3);audioPhase_=1;}else StopComment();}
      const bool tap=Sample(viewer_,{0,0},now);
      bool entryAction=false;
      for(size_t i=0;i<2 && !entryAction;++i){
        bool active=input.Entries.Pressed[i];auto& counter=entryCounters_[i];
        if(active)counter=0;else if(input.Entries.Held[i])active=OptionsRepeatDeadline(++counter,repeats_);else counter=0;
        if(active){MoveEntry(i==1,false);entryAction=true;}
      }
      if(!entryAction && (input.Accept || tap)){
        const auto old=variant_;auto next=old;
        do{next=(next+1)%int32_t(variants_.size());}while(!variants_[size_t(next)] && next!=old);
        if(next>old){variant_=next;sound(11);pending_=7;}
        else {variant_=next;MoveEntry(true,next!=old);}
      }else if(!entryAction && input.ToggleHelp)helpVisible_=!helpVisible_;
      if(input.Cancel){
        if(audioPhase_!=0 && audioPhase_!=6)StopComment();
        else {sound(4);pending_=10;imageFade_.Reverse(now);}
      }
    }
  }
  if(phase_==10){imageFade_.Tick(now);if(imageFade_.Finished()){currentBuffer_=previousBuffer_=-1;viewerVisible_=false;pending_=5;}}
  if(phase_==11){fade_.Reverse(now);pending_=12;}
  if(phase_==12){fade_.Tick(now);if(fade_.Finished())pending_=13;}
  if(phase_==13)phase_=14;
  if(phase_==14){finished_=true;return 1;}
  UpdateComment();return 0;
}
}
