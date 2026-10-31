#pragma once

#include "nativesave.h"
#include "slotdata.h"
#include <memory>

namespace Impacto::Saekano {
struct SemanticState;
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
}
