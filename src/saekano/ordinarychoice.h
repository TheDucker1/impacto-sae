#pragma once
#include "gauge.h"
#include "uisprite.h"
#include <optional>

namespace Impacto::Saekano {
struct OrdinaryChoiceInput {
  bool Previous = false, Next = false, Accept = false;
  std::optional<size_t> Released;
};
struct OrdinaryChoiceState {
  bool Started = false, Visible = false, Unselected = true;
  int32_t Phase = 0, Selected = -1;
  std::vector<ScriptLinearTween> Opacity;
  std::vector<uint8_t> Enabled;
  ScriptLinearTween Slide, Hold;
  std::vector<int32_t> Sounds;
};
// Native8d96e/8dfa0: sequential entry fade-in, explicit selection, other-entry
// fade-out, selected-row slide/hold/fade-out. A result is available only at14.
void StartOrdinaryChoice(OrdinaryChoiceState& state, size_t count,
                          const SharedSelectionEnvironment& environment, uint64_t now);
bool UpdateOrdinaryChoice(OrdinaryChoiceState& state,
                           const SharedSelectionEnvironment& environment,
                           const OrdinaryChoiceInput& input, uint64_t now);
std::optional<size_t> CompletedOrdinaryChoice(const OrdinaryChoiceState& state);
}
