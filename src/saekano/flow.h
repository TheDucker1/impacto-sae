#pragma once

#include "commands.h"

namespace Impacto::Saekano {

void AdjustDivergenceCursor(SemanticState& state);
void PruneDivergenceForJump(SemanticState& state, int32_t target);

struct DispatchState {
  bool SkipNextCommand = false;
  std::array<std::vector<size_t>, 5> LaneQueues;
};
struct CompletedCommand {
  bool QueueForLane = false;
  size_t Lane = 0;
};
enum class DispatchResult { StartNext, DeferredStart, FileRequested, ScriptEnded };

// Call only after observing a main command's completion. This does not execute
// Start or lane updates: the owning VM must perform those lifecycle operations.
DispatchResult DispatchCompleted(SemanticState& state, DispatchState& dispatcher,
                                 size_t commandCount, CompletedCommand command = {});

}  // namespace Impacto::Saekano
