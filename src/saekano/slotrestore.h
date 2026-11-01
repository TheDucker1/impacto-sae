#pragma once

#include "nativesave.h"
#include "slotdata.h"
#include "slotbrowser.h"
#include "particle.h"
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
// Scene-menu Load stops BGM using its current attached fade, then SE2/3.
// Remembered manager fields and persistent history are not changed.
void QueueNativeSlotLoadStops(SemanticState& state);
struct NativeSlotDeferredStart {
  int32_t Phase = 1;
  std::optional<int32_t> PendingPhase;
  SlotFrameChannel Channel{255, 255, 0, 12, 0};
  bool CommandStartIssued = false;
};
class BgmResources;
class SeResources;
// Call once per manager update after FILE_READY. True grants exactly one
// command Start, on update12. Playback events publish only on the first update;
// resource failure leaves the gate and pending event stream unchanged.
bool StepNativeSlotDeferredStart(SemanticState& state,
                                 NativeSlotDeferredStart& restore,
                                 bool resourcesReady,
                                 BgmResources* music, SeResources* sounds);
struct NativeSavedParticleRequest {
  size_t Row = 0, Effect = 0;
  ParticleEffectRequest Request;
  int32_t PrewarmMilliseconds = 0;
};
// Full-slot Load constructs a new context. Its unsaved model parameter slots
// retain constructor defaults (-1, +0), rather than the old live scene slots.
// Publish records only after the backend's transactional asset/SDK restore.
void RestoreNativeSlotModels(SemanticState& state, const NativeContextSave& saved,
                             Live2dDriver& driver, uint64_t now);
std::vector<NativeSavedParticleRequest> BuildNativeSlotParticleRequests(
    const NativeContextSave& saved, const SceneCameraParameters& camera,
    const ParticleEnvironment& environment, uint32_t opacityGroups);
// Called after the native-equivalent script reset/reseed. Loads all needed
// original effect assets before starting any handles; it never resets old RNG
// state a second time or resumes captured particle heaps.
std::vector<NativeSavedParticleRequest> RestoreNativeSlotParticles(
    ParticleManager& manager, const GameResources& resources,
    const NativeContextSave& saved, const SceneCameraParameters& camera,
    const ParticleEnvironment& environment, uint32_t opacityGroups);
}
