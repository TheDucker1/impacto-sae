#include "flow.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace Impacto::Saekano {
namespace {
int32_t Signed16(int32_t word) {
  return std::bit_cast<int16_t>(uint16_t(uint32_t(word)));
}
void CheckCursor(int32_t cursor, size_t count) {
  if (cursor < 0 || size_t(cursor) >= count)
    throw FormatError("dispatch destination outside command vector");
}
}  // namespace

void AdjustDivergenceCursor(SemanticState& state) {
  // Native visits newest to oldest, restarting if any node was removed. A
  // cursor remap alone does not restart the pass. Keep those behaviors distinct.
  bool removed;
  do {
    removed = false;
    for (size_t i = state.DivergenceStack.size(); i > 0; --i) {
      const auto& node = state.DivergenceStack[i - 1];
      const auto start = Signed16(node[0]), alternate = Signed16(node[1]);
      const auto resume = Signed16(node[2]), cursor = Signed16(state.CurrentCommandIndex);
      bool erase = false;
      if (start == -1 && alternate == -1) {
        state.CurrentCommandIndex = resume;
        erase = true;
      } else if (start == -1) {
        if (cursor < alternate) state.CurrentCommandIndex = alternate;
        else if (cursor >= resume) erase = true;
      } else if ((alternate == -1 && cursor >= resume) ||
                 (alternate != -1 && cursor >= alternate)) {
        state.CurrentCommandIndex = resume;
        erase = true;
      }
      if (erase) {
        state.DivergenceStack.erase(state.DivergenceStack.begin() + (i - 1));
        removed = true;
      }
    }
  } while (removed);
}

void PruneDivergenceForJump(SemanticState& state, int32_t target) {
  const int32_t truncated = Signed16(target);
  std::erase_if(state.DivergenceStack, [truncated](const auto& node) {
    const auto start = Signed16(node[0]), alternate = Signed16(node[1]);
    // Two comparisons intentionally use full words, matching 743c4/743ea.
    return (alternate != -1 && truncated > alternate) ||
           (start == -1 && truncated <= alternate) ||
           truncated <= node[0] || truncated >= node[2];
  });
}

DispatchResult DispatchCompleted(SemanticState& state, DispatchState& dispatcher,
                                 size_t commandCount, CompletedCommand command) {
  if (commandCount > size_t(std::numeric_limits<int32_t>::max()))
    throw FormatError("script exceeds signed cursor domain");
  CheckCursor(state.CurrentCommandIndex, commandCount);
  if (state.PendingFileId != -2) {
    state.DivergenceStack.clear();
    state.ExitRequested = 1;
    return DispatchResult::FileRequested;
  }
  if (state.PendingCommandIndex != -1) {
    CheckCursor(state.PendingCommandIndex, commandCount);
    state.CurrentCommandIndex = state.PendingCommandIndex;
    state.PendingCommandIndex = -1;
    PruneDivergenceForJump(state, state.CurrentCommandIndex);
  } else if (dispatcher.SkipNextCommand) {
    const auto next = int64_t(state.CurrentCommandIndex) + 2;
    if (next >= int64_t(commandCount)) throw FormatError("skip-next destination outside script");
    state.CurrentCommandIndex = int32_t(next);
    dispatcher.SkipNextCommand = false;
  } else {
    if (state.ExitRequested)
      throw UnsupportedCommand("outer end-gate cleanup policy is not implemented");
    if (command.QueueForLane) {
      dispatcher.LaneQueues.at(command.Lane).push_back(size_t(state.CurrentCommandIndex));
      if (state.FirstAsyncCommandIndex == -1)
        state.FirstAsyncCommandIndex = state.CurrentCommandIndex;
    }
    ++state.CurrentCommandIndex;
    if (size_t(state.CurrentCommandIndex) >= commandCount) {
      state.ExitRequested = 1;
      return DispatchResult::ScriptEnded;
    }
    AdjustDivergenceCursor(state);
    CheckCursor(state.CurrentCommandIndex, commandCount);
  }
  return state.SkipPhase == 2 ? DispatchResult::DeferredStart : DispatchResult::StartNext;
}

}  // namespace Impacto::Saekano
