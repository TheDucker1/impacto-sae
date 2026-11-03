#pragma once
#include "album.h"
#include "battlewidgets.h"
#include "slotbrowser.h"
#include "titlecontroller.h"

namespace Impacto::Saekano {
struct SemanticState;
struct ExtrasCgInput {
  OptionsNavigationInput Categories,Pages,Entries;
  std::array<bool,4> Grid{}; // up,down,left,right, native keys10..13.
  bool Accept=false,Cancel=false,Comment=false,ToggleHelp=false;
};
struct ExtrasCgServices {
  virtual ~ExtrasCgServices()=default;
  virtual bool ResourcesReady()const=0;
  virtual void PrepareImage(int32_t image,size_t buffer)=0;
  virtual bool ImageReady(size_t buffer)const=0;
  virtual void PrepareComment(int32_t wave)=0;
  virtual void StartComment()=0;
  virtual void StopComment()=0;
  virtual bool CommentPlaying()const=0;
  virtual bool CommentIdle()const=0;
  virtual void UiSound(int32_t id)=0;
};
// Original CG owner3b2f6. The catalogue preserves native padded card/history
// identity; image variants always address the original event-image archive.
class ExtrasCgBrowser {
 public:
  ExtrasCgBrowser(const ExtrasScreenEnvironment& environment,AlbumGalleryCatalogue catalogue,
      SemanticState& owner,ExtrasCgServices& services,std::span<const int32_t> repeats,
      uint32_t hold,bool unlockAll,uint64_t now);
  void BeginFrame(WidgetInputHistory::Snapshot contacts){contacts_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  int32_t Update(const ExtrasCgInput& input,uint64_t now);
  bool Dirty()const{return dirty_;}
  int32_t Phase()const{return phase_;}
  int32_t Category()const{return categories_.Selected;}
  int32_t SelectedCard()const{return list_.Rows.Selected*9+cell_;}
  int32_t Cell()const{return cell_;}
  int32_t Variant()const{return variant_;}
  int32_t CurrentBuffer()const{return currentBuffer_;}
  int32_t PreviousBuffer()const{return previousBuffer_;}
  int32_t ImageOpacity()const{return imageFade_.Value.Current;}
  int32_t PreviousOpacity()const{return previousFade_.Value.Current;}
  int32_t Fade()const{return fade_.Value.Current;}
  bool Viewing()const{return viewerVisible_;}
  bool HelpVisible()const{return helpVisible_;}
  bool CommentActive()const{return audioPhase_!=0;}
  const SlotBrowserState& List()const{return list_;}
  const AlbumGalleryCatalogue& Catalogue()const{return catalogue_;}
  bool Unlocked(int32_t image)const;
  bool New(size_t card)const;
  std::vector<bool> Variants(size_t card)const;
  size_t UnlockedImages()const;
  size_t TotalImages()const;
 private:
  const ExtrasScreenEnvironment& env_;AlbumGalleryCatalogue catalogue_;
  SemanticState& owner_;ExtrasCgServices& services_;
  SlotBrowserState list_;OptionsNavigationState categoryNavigation_;OptionsRowNavigation categories_;
  WidgetInputHistory contacts_;std::vector<BattleCardSprite> categoryWidgets_,cells_;
  BattleCardSprite back_,flick_,viewer_;
  TitleTween fade_,imageFade_,previousFade_;
  std::vector<bool> variants_;
  std::vector<int32_t> repeats_;std::array<int32_t,2> entryCounters_{};
  int32_t phase_=0,cell_=0,variant_=0,currentBuffer_=-1,previousBuffer_=-1,nextBuffer_=0;
  int32_t audioPhase_=0,audioReturn_=5;
  std::optional<int32_t> pending_,audioPending_;
  bool dirty_=false,helpVisible_=true,viewerVisible_=false,finished_=false;
  bool Sample(BattleCardSprite& widget,std::array<int32_t,2> offset,uint64_t now);
  void SelectPage(int32_t page);
  void SyncCategory();
  void MoveCell(int32_t delta);
  void Open(uint64_t now);
  void MoveEntry(bool forward,bool wrappedVariant);
  void StopComment();
  void UpdateComment();
};
}
