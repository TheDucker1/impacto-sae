#pragma once
#include "gauge.h"
#include "uisprite.h"
#include "ordinaryvoice.h"
#include <vector>

namespace Impacto::Saekano {
struct ChoiceMenu;
struct SemanticState;
// Static native initializer8008b454..490 ->802626b8, independent of env tables.
std::array<float,4> BattleSelectionMotionCurve();
// Original float channel9eb28/9ec48. One shot, including overshoot; callers
// decide when to restart. This must not use the oscillating profile arrows.
struct BattleCardMotion {
  float Current=0,From=0,To=0;
  std::array<float,4> Curve{};
  uint64_t Epoch=0;
  int32_t Duration=0,Elapsed=0;
  bool Enabled=false;
  void Configure(float from,float to,int32_t duration,std::array<float,4> curve,bool enabled,uint64_t now);
  void Restart(uint64_t now);
  void EnableIfDormant(uint64_t now);
  void Tick(uint64_t now);
};
struct BattleSelectionEnvironment {
  int32_t RevealDuration=0;
  std::vector<std::array<int32_t,2>> Positions,LabelPositions;
  std::vector<UiSpriteDefinition> CardSprites;
  float CardScale=0,LabelScale=0;
  std::array<std::array<int32_t,2>,2> LineOffsets{};
  struct Pulse { float From=0,To=0;int32_t Duration=0; };
  std::array<Pulse,3> Pulses{};
  std::vector<std::u16string> Labels;
  std::vector<std::array<uint8_t,4>> FillColors,FrameColors;
  int32_t MotionDuration=0,Travel=0,FadeDuration=0,TextWidth=0;
  static BattleSelectionEnvironment Decode(std::span<const uint8_t> bytes);
};
struct BattleSelectionRow {
  int32_t Destination=0,Presentation=0;
  int16_t Preview=-1;
  std::u16string Label;
};
struct BattleSelectionUiState {
  bool Known=false,Enabled=false;
  int32_t Phase=-268435456,Script=0,Command=0;
  std::optional<int32_t> PendingPhase;
  size_t Ordinal=0;
  std::vector<BattleSelectionRow> Rows;
  std::array<BattleCardMotion,3> Motions;
  std::array<ScriptLinearTween,3> Fades;
  std::array<bool,3> FadeEnabled{};
  std::array<int32_t,3> Offsets{};
  // Fresh constructor starts base7; phase0 binds11. Reset preserves the queue.
  OrdinaryVoiceQueue Voices=MakeFreshOrdinaryVoiceQueue();
  uint32_t SelectedVoiceSlot=0;
};
class BattleSelectionUiServices:public OrdinaryVoicePreparation {
 public:
  // Shared record: bind actual font owner, parse/wrap at width, unread color,
  // clear row+220 and fully reveal. The UI retains the prepared text record.
  virtual void PrepareRow(size_t row,const BattleSelectionRow& value,int32_t width)=0;
  virtual void RegisterPreview(int16_t id)=0;
  virtual void ClearControl(size_t control)=0;
  // Two unconditional native widget groups, each containing one sprite; both
  // layers draw. Geometry uses the shared bubble, not the HUD card table.
  virtual void AddControlState(size_t control,uint32_t texture,std::array<int32_t,2> position,
      float scale,std::array<uint8_t,4> color,bool interactive)=0;
  virtual void SetControlPulse(size_t control,size_t state,const BattleSelectionEnvironment::Pulse& pulse)=0;
  virtual void SetControlOffset(size_t control,int32_t y)=0;
  virtual bool FacePressed(size_t control)=0;
  virtual bool PointerPressed(size_t control)=0; // Native virtual+24: Enabled && Flags&1.
  virtual void PlayUiSound(uint32_t id)=0;
  virtual bool PreviewEnabled()const=0;
  virtual bool Playing(uint32_t channel)=0;
};
void ResetBattleSelectionUi(BattleSelectionUiState& state,const ChoiceMenu& menu,
    int32_t script,int32_t command,const BattleSelectionEnvironment& environment,uint64_t now);
void EnableBattleSelectionUi(BattleSelectionUiState& state);
// Phase only. The command host's Timeout also sets Enabled=false separately.
void TimeoutBattleSelectionUi(BattleSelectionUiState& state);
bool UpdateBattleSelectionUi(BattleSelectionUiState& state,const BattleSelectionEnvironment& environment,
    std::array<float,4> curve,BattleSelectionUiServices& services,uint64_t now);
// Command-owned countdown/reveal state; distinct from the UI's phases0..10.
struct BattleSelectionState {
  bool Started=false,Active=false,Completed=false,CountdownEnabled=false,Paused=false;
  int32_t InitialSeconds=0,Seconds=0,Script=0,Command=0;
  ScriptLinearTween Countdown,Reveal;
  std::vector<int32_t> Destinations;
};
// The actual owning UI must bind all these services. A basic ordinal/timeout
// menu is insufficient; frontends stay stopped until that consumer is ready.
class BattleSelectionHost {
 public:
  virtual ~BattleSelectionHost()=default;
  // Own actual rows/labels/signed preview IDs and decimal presentation extras.
  // Initialize absent presentation fields safely, including timeout entry0.
  virtual void Begin(const ChoiceMenu& menu,int32_t script,int32_t command)=0;
  virtual void StopDialogueVoiceNextSlots()=0;
  virtual void Enable()=0;
  virtual bool Update(uint64_t now)=0; // Native returns false only at UIphase10.
  virtual int32_t Phase()const=0;
  virtual size_t Ordinal()const=0;
  virtual void Timeout()=0; // Native immediate UIphase8 and enabled=false.
  virtual bool HistorySuppressed()const=0;
  // Touch only a registered owner-history row; do not invent restored flags.
  virtual void MarkHistory(int32_t script,int32_t command,size_t ordinal)=0;
};
void StartBattleSelection(SemanticState& owner,const ChoiceMenu& menu,
    const BattleSelectionEnvironment& environment,BattleSelectionHost& host,uint64_t now);
bool UpdateBattleSelection(SemanticState& owner,BattleSelectionHost& host,uint64_t now);
}
