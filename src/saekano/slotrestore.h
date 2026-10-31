#pragma once

#include "nativesave.h"
#include "slotdata.h"
#include <memory>

namespace Impacto::Saekano {
struct SemanticState;
struct MessageRuntimeContext;
class SceneImageDriver;
// Native selected record -> pending S5e814, protected flag cleared, load request.
// Kept separate from the current scene until the outer Load transition consumes
// it. Native pointers, thumbnails and strings have ordinary PC ownership here.
struct NativePendingSlotLoad {
  size_t Ordinal = 0;
  NativeSlotRecord Record;
  NativeContextSave Context;
};
std::unique_ptr<NativePendingSlotLoad> StageNativeSlotLoad(
    const NativeSlotData& slots, size_t ordinal);
// Working owner only: Bank0/session/background/profile stats/chapter/day/time.
// Persistent Bank1/preferences/histories and live manager fields survive this
// step. Resource/context restoration must finish separately before VM Start.
void ApplyNativeSlotWorkingOwner(SemanticState& owner,
                                 const NativePendingSlotLoad& pending);
// Verified saved manager values only. Call on a newly constructed manager;
// unsaved defaults/load side effects must already be supplied by that manager.
// This does not perform resource requests or grant FILE_READY.
void ApplyNativeSlotContextValues(SemanticState& state,
                                  MessageRuntimeContext& message,
                                  const NativePendingSlotLoad& pending);
struct NativeRuleWipeRestore {
  bool Started = false, Ready = false;
};
// Native71f78 rule branch. Requires the fresh manager's rule owner. Active
// resources are loaded by their original archive IDs, then await the real gate.
void BeginNativeRuleWipeRestore(SemanticState& state,
                                const NativeContextSave& saved,
                                SceneImageDriver& images,
                                NativeRuleWipeRestore& restore);
bool PollNativeRuleWipeRestore(const SceneImageDriver& images,
                               NativeRuleWipeRestore& restore);
}
