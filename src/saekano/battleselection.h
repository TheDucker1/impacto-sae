#pragma once
#include "gauge.h"
#include "uisprite.h"
#include <vector>

namespace Impacto::Saekano {
struct ChoiceMenu;
struct SemanticState;
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
  int32_t MotionDuration=0,Travel=0,FadeDuration=0;
  static BattleSelectionEnvironment Decode(std::span<const uint8_t> bytes);
};
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
