#pragma once

#include "scenemessage.h"
#include "profilemouth.h"
#include <map>

namespace Impacto::Saekano {
struct OrdinaryMouthControl {
  bool OverrideEnabled;
  std::array<int16_t, 5> OverrideIds;
  bool DualCleanup;
  std::optional<std::array<int32_t, 2>> CleanupIds;
};
OrdinaryMouthControl MakeFreshOrdinaryMouthControl();
struct MouthModelRecord { int32_t LogicalId; bool Attached; };
enum class MouthTargetKind { Sprite, Model };
struct MouthTarget { MouthTargetKind Kind; int32_t Id; };
// Sprite IDs describe a complete map; model records describe the complete
// ordered native52-byte table, including unattached and duplicate logical IDs.
std::vector<MouthTarget> SelectOrdinaryMouthTargets(
    const OrdinaryMouthControl& control, int32_t speaker,
    std::span<const uint16_t> spriteIds,
    std::span<const MouthModelRecord> models, bool cleanup);

// Sprite path only. The caller supplies actual known mouth channels and owns
// their eligible per-actor updates. Live2D envelope/SDK binding remains separate.
class SpriteMessageMouth final : public OrdinaryMessageMouth {
 public:
  SpriteMessageMouth(const SemanticState& state, const OrdinaryMouthControl& control,
                     std::map<uint16_t, ProfileMouthPlayback>& channels)
      : state_(state), control_(control), channels_(channels) {}
  void Begin(int32_t speaker, const ProfileMouthAnimation& envelope,
             uint64_t now) override;
  void End(int32_t speaker) override;
 private:
  const SemanticState& state_;
  const OrdinaryMouthControl& control_;
  std::map<uint16_t, ProfileMouthPlayback>& channels_;
  std::vector<uint16_t> Targets(int32_t speaker, bool cleanup) const;
};
}  // namespace Impacto::Saekano
