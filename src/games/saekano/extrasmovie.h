#pragma once
#include "album.h"
#include "slotbrowser.h"
#include "battlewidgets.h"
#include "profilearrow.h"
#include "systemsave.h"

namespace Impacto::Saekano {
struct SemanticState;
struct ExtrasMovieInput {OptionsNavigationInput Horizontal;bool Accept=false,Cancel=false;};
struct ExtrasMovieServices {
  virtual ~ExtrasMovieServices()=default;
  virtual bool ResourcesReady()const=0;
  virtual void PrepareMovie(size_t index)=0;
  virtual void ReprepareMovie()=0;
  virtual bool MovieReady()const=0;
  virtual bool MovieBusy()const=0; // Native busy includes preparation/preroll.
  virtual bool MovieIdle()const=0;
  virtual bool MoviePaused()const=0;
  virtual void PauseMovie()=0;
  virtual void StartMovie()=0;
  virtual void StopMovie()=0;
  virtual uint64_t MoviePositionUs()const=0;
  virtual void UiSound(int32_t id)=0;
  virtual void RestoreExtrasMusic()=0;
};
// Native movie owner403e4, using the already-established horizontal mapping of
// the shared list/frame channels, widgets and script tweens.
class ExtrasMovieBrowser {
 public:
  ExtrasMovieBrowser(const ExtrasScreenEnvironment& environment,SemanticState& owner,
      ExtrasMovieServices& services,std::span<const int32_t> repeats,uint32_t hold,
      bool unlockAll,uint64_t now);
  void BeginFrame(WidgetInputHistory::Snapshot contacts){contacts_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  int32_t Update(const ExtrasMovieInput& input,uint64_t now); //1 returns to Extras.
  bool Dirty()const{return dirty_;}
  bool Unlocked(size_t index)const;
  int32_t Status(size_t index)const;
  int32_t Phase()const{return phase_;}
  int32_t Selected()const{return list_.Rows.Selected;}
  int32_t PreviewIndex()const{return preview_;}
  bool PreviewHidden()const{return previewHidden_;}
  bool Browsing()const{return browsing_;}
  int32_t Fade()const{return fade_.Value.Current;}
  const SlotBrowserState& List()const{return list_;}
  const ProfileArrowMotion& Arrows()const{return arrows_;}
  std::array<int32_t,4> MovieRectangle()const;
  const ExtrasScreenEnvironment& Environment()const{return env_;}
 private:
  const ExtrasScreenEnvironment& env_;SemanticState& owner_;ExtrasMovieServices& services_;
  SlotBrowserState list_;WidgetInputHistory contacts_;ProfileArrowMotion arrows_;
  std::array<TitleTween,4> movieRect_;
  TitleTween fade_,delay_;
  BattleCardSprite back_,play_,flick_,left_,right_;
  int32_t phase_=0,preview_=-1,cycle_=2;
  std::optional<int32_t> pending_;
  bool dirty_=false,browsing_=true,previewHidden_=false,changed_=false,finished_=false;
  bool Sample(BattleCardSprite& widget,std::array<int32_t,2> offset,uint64_t now);
  void Changed(uint64_t now);
  void OpenMovie(uint64_t now);
  void TickRectangle(uint64_t now);
};
}
