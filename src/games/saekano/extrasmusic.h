#pragma once
#include "album.h"
#include "battlewidgets.h"
#include "slotbrowser.h"
#include "titlecontroller.h"

namespace Impacto::Saekano {
struct SemanticState;
struct ExtrasMusicInput {
  OptionsNavigationInput Navigation; // Native keys12/13 in the music owner.
  bool Accept=false,Cancel=false,Stop=false,Mode=false;
};
struct ExtrasMusicServices {
  virtual ~ExtrasMusicServices()=default;
  virtual void PlayMusic(int32_t wave)=0;
  virtual void StopMusic()=0;
  virtual void SetPaused(bool paused)=0;
  virtual bool Paused()const=0;
  virtual bool Busy()const=0;
  virtual void RestoreExtrasMusic()=0;
  virtual void UiSound(int32_t id)=0;
};
// Native43570/4404a/43b34/43cb0/43fd4. List row, bank wave and saved-history
// identity are distinct; the authored order determines repeat-all playback.
class ExtrasMusicBrowser {
 public:
  ExtrasMusicBrowser(const ExtrasScreenEnvironment& environment,SemanticState& owner,
      ExtrasMusicServices& services,std::span<const int32_t> repeats,uint32_t hold,
      bool unlockAll,uint64_t now);
  void BeginFrame(WidgetInputHistory::Snapshot contacts){contacts_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  int32_t Update(const ExtrasMusicInput& input,uint64_t now);
  bool Dirty()const{return dirty_;}
  int32_t Phase()const{return phase_;}
  int32_t Selected()const{return list_.Rows.Selected;}
  int32_t PlayingRow()const{return playing_;}
  int32_t Mode()const{return mode_;} // 0 single,1 cycle unlocked,2 repeat current.
  bool PlayIconActive()const{return playIcon_;}
  int32_t Fade()const{return fade_.Value.Current;}
  int32_t Status(size_t row)const;
  bool Unlocked(size_t row)const;
  size_t UnlockedCount()const;
  const SlotBrowserState& List()const{return list_;}
 private:
  const ExtrasScreenEnvironment& env_;SemanticState& owner_;ExtrasMusicServices& services_;
  SlotBrowserState list_;WidgetInputHistory contacts_;TitleTween fade_;
  BattleCardSprite play_,stop_,modeButton_,back_,flick_;std::vector<BattleCardSprite> rows_;
  std::vector<int32_t> playlist_;size_t playlistIndex_=0;
  int32_t phase_=0,playing_=-1,mode_=0;
  std::optional<int32_t> pending_;
  bool dirty_=false,playIcon_=false,finished_=false;
  void Sample(BattleCardSprite& widget,uint64_t now);
  void BuildPlaylist(int32_t first);
  void Play(int32_t row);
  void Activate();
};
}
