#pragma once
#include "battlepresentation.h"
#include "scenemessage.h"
#include "skipauto.h"
#include "audio.h"

namespace Impacto::Saekano {
// Owns the actual selection UI and joins its command, presentation, registered
// history and physical audio services. Platform input still arrives per frame.
// Construct with proven fresh/restored history and settings, never placeholders.
class ArchiveBattleSelectionHost final:public BattleSelectionHost,public BattleSelectionPresentation {
 public:
  ArchiveBattleSelectionHost(const BattleSelectionEnvironment& environment,
      UiSpriteDefinition sharedBubble,uint32_t holdFrames,const SharedSelectionEnvironment& selection,
      const OrdinaryTextEnvironment& text,const TextFormatEnvironment& format,const GsfFont& font,
      TextLayoutMetrics metrics,uint32_t utilitySize,uint32_t nominalSize,
      const AwbIndex& voiceBank,SceneMessageVoiceOwner& voices,SemanticState& owner,
      RegisteredReadHistory& history,HistoryEnvironment historyEnvironment);
  void BeginFrame(uint64_t now,WidgetInputHistory::Snapshot contacts,uint32_t faceMask);
  void Begin(const ChoiceMenu& menu,int32_t script,int32_t command)override;
  void StopDialogueVoiceNextSlots()override;
  void Enable()override{EnableBattleSelectionUi(state_);}
  bool Update(uint64_t now)override;
  int32_t Phase()const override{return state_.Phase;}
  size_t Ordinal()const override{return state_.Ordinal;}
  void Timeout()override{TimeoutBattleSelectionUi(state_);state_.Enabled=false;}
  bool HistorySuppressed()const override{return historyEnvironment_.SuppressReadHistory!=0;}
  void MarkHistory(int32_t script,int32_t command,size_t ordinal)override;
  void RegisterPreview(int16_t id)override;
  void Stop(uint32_t channel)override;
  void Prepare(uint32_t channel,int16_t id)override;
  void Start(uint32_t channel)override;
  bool Playing(uint32_t channel)override;
  bool PreviewEnabled()const override{return voices_.Settings.Enabled[7]!=0;}
  void PlayUiSound(uint32_t id)override;
  std::vector<BattleSelectionDraw> Draw()const;
  const BattleSelectionUiState& State()const{return state_;}
 private:
  const BattleSelectionEnvironment& environment_;
  const AwbIndex& voiceBank_;
  SceneMessageVoiceOwner& voices_;
  SemanticState& owner_;
  RegisteredReadHistory& history_;
  HistoryEnvironment historyEnvironment_;
  BattleSelectionUiState state_;
  std::optional<uint64_t> now_;
  void RequireFrame()const;
  static void ValidateChannel(uint32_t channel);
  ReadHistoryBranch* HistoryRow(int32_t script,int32_t command)const;
};
}
