#pragma once

#include "formats.h"
#include <optional>

namespace Impacto::Saekano {
struct SemanticState;
struct ApproachEntryState {
  std::optional<bool> Enabled;  // C14517; unknown in portable restores.
  std::optional<int32_t> Id;    // C14534.
};
struct ApproachResourceStatus {
  uint32_t QueuedJobs = 0;  // C1c80, read/create items still in the queue.
  bool PendingEnabled = false;  // C1c85.
  uint32_t PendingCount = 0;    // C1ca0-C1c9c, represented as a count.
  bool Idle() const { return !QueuedJobs && (!PendingEnabled || !PendingCount); }
};
class ApproachResources {
 public:
  virtual ~ApproachResources() = default;
  virtual void Begin(int32_t id) = 0;
  virtual ApproachResourceStatus Status() const = 0;
  virtual void Reset() = 0;
};
// Native8007df22/b2/d0; this entry loads mode textures, not a whole mission.
void StartApproachEntry(SemanticState& state, int32_t id, ApproachResources& resources);
bool UpdateApproachEntry(SemanticState& state, const ApproachResources& resources, bool skipping);
}
