#include "approachentry.h"
#include "commands.h"

namespace Impacto::Saekano {
void StartApproachEntry(SemanticState& state, int32_t id, ApproachResources& resources) {
  if (id < 0) throw FormatError("negative approach resource ID");
  resources.Begin(id);
  state.ApproachMode = {true, id};
  state.ApproachGauge = 0;
  state.DayEnabled = 0;
  state.SkipPhase = 2;
  // Direct service stops; preserve script context's remembered SE fields.
  state.PendingEvents.push_back({GameEventKind::StopSe, 2});
  state.PendingEvents.push_back({GameEventKind::StopSe, 3});
}
bool UpdateApproachEntry(SemanticState& state, const ApproachResources& resources, bool skipping) {
  const bool done = resources.Status().Idle();
  if (skipping) state.SkipPhase = done ? 1 : 2;
  return done;
}
}
