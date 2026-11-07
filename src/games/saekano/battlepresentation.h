#pragma once
#include "battletext.h"
#include "battlewidgets.h"

namespace Impacto::Saekano {
struct BattleSelectionDraw {
  bool IsText=false;
  UiSpriteBlit Sprite;
  UiTextBlit Text;
};
struct BattleVersusVisualEnvironment;
std::vector<UiSpriteBlit> BuildBattleUnderlay(const BattleVersusVisualEnvironment& environment,const SemanticState& state);
std::vector<UiSpriteBlit> BuildBattleGaugeDraw(const BattleVersusVisualEnvironment& environment,const SemanticState& state);
std::vector<UiSpriteBlit> BuildBattleSelectionFrame(const BattleSelectionEnvironment& environment,bool enabled,int32_t seconds);
// Real text/widget/input implementation of the UI service contract. The owning
// backend must supply descriptor/audio, settings and UI-sound services; these
// remain abstract so missing physical playback cannot silently complete a choice.
class BattleSelectionPresentation:public BattleSelectionUiServices {
 public:
  BattleSelectionPresentation(const BattleSelectionEnvironment& environment,
      UiSpriteDefinition sharedBubble,uint32_t holdFrames,const SharedSelectionEnvironment& selection,
      const OrdinaryTextEnvironment& text,const TextFormatEnvironment& format,const GsfFont& font,
      TextLayoutMetrics metrics,uint32_t utilitySize,uint32_t nominalSize);
  void BeginFrame(uint64_t now,WidgetInputHistory::Snapshot contacts,uint32_t faceMask);
  void CancelPointer(){widgets_.CancelPointer();}
  void PrepareRow(size_t row,const BattleSelectionRow& value,int32_t width)override;
  void ClearControl(size_t control)override{widgets_.Clear(control);}
  void AddControlState(size_t control,uint32_t texture,std::array<int32_t,2> position,
      float scale,std::array<uint8_t,4> color,bool interactive)override;
  void SetControlPulse(size_t control,size_t state,const BattleSelectionEnvironment::Pulse& pulse)override;
  void SetControlOffset(size_t control,int32_t y)override;
  bool FacePressed(size_t control)override;
  bool PointerPressed(size_t control)override{return widgets_.Pressed(control);}
  // Empty history means no registered entry, as in native Draw. Restored owner
  // validity must be established by the host before it enables this UI.
  std::vector<BattleSelectionDraw> Draw(const BattleSelectionUiState& state,
      bool historyColors,bool suppressed,std::span<const uint8_t> history)const;
  const BattleChoiceText& Text(size_t row)const{return texts_.at(row);}
  const BattleSelectionWidgets& Widgets()const{return widgets_;}
 private:
  const BattleSelectionEnvironment& environment_;
  const SharedSelectionEnvironment& selection_;
  const OrdinaryTextEnvironment& text_;
  const TextFormatEnvironment& format_;
  const GsfFont& font_;
  TextLayoutMetrics metrics_;
  uint32_t utility_,nominal_,faces_=0;
  uint64_t now_=0;
  bool sampled_=false;
  BattleSelectionWidgets widgets_;
  std::array<BattleChoiceText,4> texts_;
  void RequireFrame()const;
};
}
