#pragma once
#include "album.h"
#include "battlewidgets.h"
#include "options.h"
#include "profilearrow.h"
#include "slotbrowser.h"

namespace Impacto::Saekano {
struct SemanticState;
struct ExtrasBattleInput {OptionsNavigationInput Navigation;bool Accept=false,Cancel=false;};
struct ExtrasBattleServices {
  virtual ~ExtrasBattleServices()=default;
  virtual bool ResourcesReady()const=0;
  virtual void UiSound(int32_t id)=0;
  virtual void BeginConfirmation()=0;
  virtual OptionsDialogPoll PollConfirmation()=0;
  virtual void StopMusic()=0;
};
// Native4767c/48f66/49122. Phase7 hands the selected script to the actual
// scenario session; the owner resumes this browser only after that session retires.
class ExtrasBattleBrowser {
 public:
  ExtrasBattleBrowser(const ExtrasScreenEnvironment& environment,SemanticState& owner,
      ExtrasBattleServices& services,std::span<const int32_t> repeats,uint32_t hold,
      bool unlockAll,uint64_t now);
  void BeginFrame(WidgetInputHistory::Snapshot contacts){contacts_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  int32_t Update(const ExtrasBattleInput& input,uint64_t now);
  bool ReplayRequested()const{return phase_==7;}
  void ResumeReplay(uint64_t now);
  bool Dirty()const{return dirty_;}
  int32_t Phase()const{return phase_;}
  int32_t Selected()const{return list_.Rows.Selected;}
  int32_t Fade()const{return fade_.Value.Current;}
  int32_t Status(size_t row)const;
  bool Unlocked(size_t row)const;
  size_t UnlockedCount()const;
  const SlotBrowserState& List()const{return list_;}
  const ProfileArrowMotion& Arrows()const{return arrows_;}
 private:
  const ExtrasScreenEnvironment& env_;SemanticState& owner_;ExtrasBattleServices& services_;
  SlotBrowserState list_;WidgetInputHistory contacts_;ProfileArrowMotion arrows_;
  TitleTween fade_;BattleCardSprite back_,flick_;std::vector<BattleCardSprite> rows_;
  int32_t phase_=0,returnPhase_=5;std::optional<int32_t> pending_;
  bool dirty_=false,finished_=false;
  void Sample(BattleCardSprite& widget,uint64_t now);
  void Confirm();
};
}
