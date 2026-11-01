#pragma once

#include "formats.h"

#include <array>
#include <optional>

namespace Impacto::Saekano {

struct Live2dCommand {
  uint32_t Selector = 0;
  // Load2 uses Target as the archive/cache resource ID and Value as logical ID.
  // Every other indexed operation addresses the record's logical ID via Target.
  int32_t Target = 0;
  int32_t Value = 0;
  std::array<float, 2> Vector{};
  int32_t Transition = 0;
  std::array<uint8_t, 4> Color{};  // RGBA, after native byte truncation.
};
Live2dCommand DecodeLive2d(std::span<const uint8_t> payload);

struct Live2dRecord {
  int32_t LogicalId = 0;
  std::optional<int32_t> ResourceId;
  // Native insertion does not initialize every scalar. Preserve 'not assigned'
  // rather than copying stack garbage or claiming a value was captured.
  std::optional<std::array<float, 2>> Position;
  std::optional<std::array<float, 2>> Scale;
  std::optional<bool> Enabled;
  std::optional<std::array<uint8_t, 4>> Color = std::array<uint8_t, 4>{};
  std::optional<int32_t> AnimationIndex;
  std::optional<int16_t> Pose;
};
struct Live2dParameterSlot {
  int32_t Target = -1;
  float Value = 0;
};
struct Live2dState {
  // A new native context has an empty vector. Restored callers must provide its
  // actual records/slots; an omitted vector is not a known empty scene.
  bool Known = true;
  std::vector<Live2dRecord> Records;
  std::array<Live2dParameterSlot, 2> Parameters;
};
std::optional<float> Live2dParameterValue(const Live2dState& state,
                                          int32_t logicalId);

// Implemented by the model/resource backend, not by a permissive no-op
// fallback. It owns parsed SDK objects and their game-specific motion/eye/tween
// runtime.
struct Live2dTransform {
  std::array<float, 2> Position{}, Scale{};
};
class Live2dDriver {
 public:
  virtual ~Live2dDriver() = default;
  virtual bool Supports(uint32_t selector) const = 0;
  virtual bool GateReady() const = 0;
  virtual bool ResourceReady(int32_t resource) const = 0;
  virtual void RequestResource(int32_t resource, int32_t logical,
                               uint64_t now) = 0;
  virtual void Attach(const Live2dRecord& record,
                      int32_t configurationLogicalId, uint64_t now) = 0;
  virtual void Apply(const Live2dRecord& record, const Live2dCommand& command,
                     uint64_t now) = 0;
  virtual void Remove(const Live2dRecord& record) = 0;
  virtual void Reset() = 0;
  // Native save/load reattaches saved targets to fresh animation owners.
  // Concrete drivers must prepare transactionally before replacing the scene.
  virtual void Restore(const Live2dState& state, uint64_t now);
  // Game mode effects operate on current renderer transforms and do not alter
  // script-record targets (which are used by native save serialization).
  virtual Live2dTransform CurrentTransform(const Live2dRecord& record) const;
  virtual void SetTransform(const Live2dRecord& record, const Live2dTransform& transform,
                            uint64_t now);
};

void StartLive2d(Live2dState& state, const Live2dCommand& command,
                 Live2dDriver& driver, uint64_t now);
bool UpdateLive2d(Live2dState& state, const Live2dCommand& command,
                  Live2dDriver& driver, bool skipping, uint64_t now);

}  // namespace Impacto::Saekano
