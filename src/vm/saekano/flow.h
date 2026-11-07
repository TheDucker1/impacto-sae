#pragma once

#include "commands.h"
#include <functional>

namespace Impacto::Saekano {

void AdjustDivergenceCursor(SemanticState& state);
void PruneDivergenceForJump(SemanticState& state, int32_t target);

struct DispatchState {
  bool SkipNextCommand = false;
  bool FlushLanes = false;
  std::array<std::vector<size_t>, 5> LaneQueues;
};
// Main command and lane commands share their mutable runtime object. Callback
// receives its main command index; skipping/flush ignores the completion result.
void ProcessLanes(SemanticState& state, DispatchState& dispatcher,
                  const std::function<bool(size_t, UpdateRoute)>& update);
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
