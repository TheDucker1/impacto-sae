#pragma once

#include "scenemessage.h"
#include "profilemouth.h"
#include "modelmouth.h"
#include "mouthcontrol.h"
#include <map>

namespace Impacto::Saekano {
struct MouthModelRecord { int32_t LogicalId; bool Attached; };
enum class MouthTargetKind { Sprite, Model };
struct MouthTarget { MouthTargetKind Kind; int32_t Id; };
// Sprite IDs describe a complete map; model records describe the complete
// ordered native52-byte table, including unattached and duplicate logical IDs.
std::vector<MouthTarget> SelectOrdinaryMouthTargets(
    const OrdinaryMouthControl& control, int32_t speaker,
    std::span<const uint16_t> spriteIds,
    std::span<const MouthModelRecord> models, bool cleanup);

// The caller owns sprite update eligibility and supplies model envelope services
// when attached model records participate in the native target-selection path.
class SpriteMessageMouth final : public OrdinaryMessageMouth {
 public:
  SpriteMessageMouth(const SemanticState& state, const OrdinaryMouthControl& control,
                     std::map<uint16_t, ProfileMouthPlayback>& channels,
                     OrdinaryModelMouthServices* models=nullptr)
      : state_(state), control_(&control), channels_(channels),models_(models) {}
  SpriteMessageMouth(const SemanticState& state,
                     std::map<uint16_t, ProfileMouthPlayback>& channels,
                     OrdinaryModelMouthServices* models=nullptr)
      : state_(state), channels_(channels),models_(models) {}
  void Begin(int32_t speaker, const ProfileMouthAnimation& envelope,
             uint64_t now) override;
  void End(int32_t speaker) override;
 private:
  const SemanticState& state_;
  const OrdinaryMouthControl* control_=nullptr;
  std::map<uint16_t, ProfileMouthPlayback>& channels_;
  OrdinaryModelMouthServices* models_;
  std::vector<MouthTarget> Targets(int32_t speaker, bool cleanup) const;
  int32_t Resource(int32_t logical) const;
};
}  // namespace Impacto::Saekano
