#pragma once
#include "backlog.h"
#include "slotviewport.h"
#include "battlewidgets.h"
#include "options.h"
#include "titlecontroller.h"
#include "profilearrow.h"
#include <bit>
#include <map>

namespace Impacto::Saekano {
// Keys retain native global low addresses (base80250000), not wire offsets.
struct BacklogEnvironment {
  using Point=std::array<int32_t,2>;using Rectangle=std::array<int32_t,4>;using Color=std::array<uint8_t,4>;
  std::array<uint8_t,3> Flags{};
  std::map<uint16_t,uint32_t> Scalars;
  std::map<uint16_t,Point> Points;
  std::map<uint16_t,Rectangle> Rectangles;
  std::map<uint16_t,Color> Colors;
  std::map<uint16_t,UiSpriteDefinition> Sprites;
  std::vector<Rectangle> RowRectangles;
  std::vector<UiSpriteDefinition> VoiceIcons;
  std::vector<int32_t> Unused;
  std::u16string RewindQuestion;
  BacklogTextEnvironment Text;
  static BacklogEnvironment Decode(std::span<const uint8_t> bytes);
  int32_t Word(uint16_t at)const{return std::bit_cast<int32_t>(Scalars.at(at));}
};
struct BacklogInput {OptionsNavigationInput Vertical;bool Replay=false,Rewind=false,Cancel=false;};
struct BacklogScreenServices {
  virtual ~BacklogScreenServices()=default;
  virtual void UiSound(int32_t id)=0;
  virtual bool VoicePlaying(size_t row)const=0;
  virtual void ToggleVoice(size_t row)=0;
  virtual void BeginRewindQuestion(const std::u16string& prompt)=0;
  virtual OptionsDialogPoll PollRewindQuestion()=0;
  virtual void RetireRewindQuestion()=0;
};
class BacklogScreen {
 public:
  BacklogScreen(BacklogEnvironment environment,size_t count,std::span<const int32_t> repeats,uint32_t hold,BacklogScreenServices& services);
  void BeginFrame(WidgetInputHistory::Snapshot contacts){contacts_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  //0 owned,1 dismissed,2 confirmed rewind. Selected remains a chronological row.
  int32_t Update(const BacklogInput& input,uint64_t now);
  int32_t Phase()const{return phase_;}
  int32_t Selected()const{return list_.Rows.Selected;}
  const SlotBrowserState& List()const{return list_;}
  const BacklogEnvironment& Environment()const{return env_;}
  int32_t Fade()const{return fade_.Value.Current;}
  int32_t RewindFade()const{return rewindFade_.Value.Current;}
  int32_t VoiceIcon()const{return icon_.Channel.Current;}
  float ArrowOffset()const{return arrow_.Current;}
  SlotViewportInputs Viewport()const;
  std::vector<UiSpriteBlit> Widgets()const;
 private:
  BacklogEnvironment env_;size_t count_;BacklogScreenServices& services_;
  SlotBrowserState list_;WidgetInputHistory contacts_;
  std::vector<BattleCardSprite> rows_,arrows_;BattleCardSprite back_,backContact_;
  TitleTween fade_,rewindFade_;ScriptReversingTween icon_;
  int32_t phase_=0;std::optional<int32_t> pendingPhase_;
  ProfileArrowMotion arrow_;
  void Sample(BattleCardSprite& body,std::array<int32_t,2> offset,uint64_t now);
};
}
