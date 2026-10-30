#pragma once
#include "gauge.h"
#include <vector>

namespace Impacto::Saekano {
struct ChoiceMenu;
struct SemanticState;
struct BattleSelectionEnvironment { int32_t RevealDuration=0; };
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
