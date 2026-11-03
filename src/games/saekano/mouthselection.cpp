#include "mouthselection.h"
#include <algorithm>

namespace Impacto::Saekano {
OrdinaryMouthControl MakeFreshOrdinaryMouthControl() {
  return {false, {0, 0, 0, 0, 0}, false, std::nullopt};
}
std::vector<MouthTarget> SelectOrdinaryMouthTargets(
    const OrdinaryMouthControl& control, int32_t speaker,
    std::span<const uint16_t> spriteIds,
    std::span<const MouthModelRecord> models, bool cleanup) {
  std::vector<uint16_t> sprites(spriteIds.begin(), spriteIds.end());
  std::sort(sprites.begin(), sprites.end());
  if (std::adjacent_find(sprites.begin(), sprites.end()) != sprites.end())
    throw FormatError("mouth sprite map has duplicate keys");
  std::vector<MouthTarget> result;
  if (control.OverrideEnabled) {
    for (const auto id : control.OverrideIds) {
      if (id < 0) continue;
      if (!std::binary_search(sprites.begin(), sprites.end(), uint16_t(id)))
        throw UnsupportedCommand("explicit mouth actor is unavailable");
      result.push_back({MouthTargetKind::Sprite, id});
    }
  } else if (models.empty()) {
    if (cleanup) {
      for (const auto id : sprites) result.push_back({MouthTargetKind::Sprite, id});
    } else if (speaker >= 0) {
      if (speaker > UINT16_MAX)
        throw FormatError("mouth speaker exceeds sprite map key range");
      if (std::binary_search(sprites.begin(), sprites.end(), uint16_t(speaker)))
        result.push_back({MouthTargetKind::Sprite, speaker});
    }
  } else if (speaker >= 0) {
    const auto append = [&](int32_t id) {
      const auto found = std::find_if(models.begin(), models.end(),
          [&](const auto& record) { return record.LogicalId == id; });
      if (found != models.end() && found->Attached)
        result.push_back({MouthTargetKind::Model, id});
    };
    if (cleanup && control.DualCleanup) {
      if (!control.CleanupIds)
        throw UnsupportedCommand("dual mouth cleanup actor IDs are unknown");
      for (const auto id : *control.CleanupIds) append(id);
    } else append(speaker);
  }
  return result;
}
int32_t SpriteMessageMouth::Resource(int32_t logical) const {
  const auto found=std::find_if(state_.Live2d.Records.begin(),state_.Live2d.Records.end(),
      [&](const auto& record){return record.LogicalId==logical;});
  if(found==state_.Live2d.Records.end() || !found->ResourceId)
    throw UnsupportedCommand("selected model mouth resource is unavailable");
  return *found->ResourceId;
}
std::vector<MouthTarget> SpriteMessageMouth::Targets(int32_t speaker, bool cleanup) const {
  if (!state_.Live2d.Known || !state_.CharacterRuntimeState.Known)
    throw UnsupportedCommand("ordinary mouth actor extent is unknown");
  const auto* control=control_?control_:state_.MouthControl?&*state_.MouthControl:nullptr;
  if(!control)throw UnsupportedCommand("ordinary mouth control state is unknown");
  if (!models_ && !control->OverrideEnabled && !state_.Live2d.Records.empty())
    throw UnsupportedCommand("ordinary Live2D mouth service is not bound");
  std::vector<uint16_t> keys;
  for (const auto& [id, actor] : state_.Characters) keys.push_back(id);
  std::vector<MouthModelRecord> records;
  for(const auto& record:state_.Live2d.Records)records.push_back({record.LogicalId,record.ResourceId.has_value()});
  const auto selected = SelectOrdinaryMouthTargets(*control, speaker, keys, records, cleanup);
  for (const auto& target : selected) {
    if(target.Kind==MouthTargetKind::Model) {
      if(!models_ || !models_->MouthResourceReady(Resource(target.Id)))
        throw UnsupportedCommand("selected model mouth resource is not ready");
    } else {
      const auto id = uint16_t(target.Id);
      if (!state_.Characters.at(id).RuntimeKnown || !channels_.contains(id))
        throw UnsupportedCommand("selected actor mouth state is unknown");
    }
  }
  return selected;
}
void SpriteMessageMouth::Begin(int32_t speaker,
    const ProfileMouthAnimation& envelope, uint64_t now) {
  for (const auto target : Targets(speaker, false)) {
    if(target.Kind==MouthTargetKind::Model)models_->BeginMouthEnvelope(Resource(target.Id),envelope,now);
    else channels_.at(uint16_t(target.Id)).Begin(envelope,now);
  }
}
void SpriteMessageMouth::End(int32_t speaker) {
  for (const auto target : Targets(speaker, true)) {
    if(target.Kind==MouthTargetKind::Model)models_->EndMouthEnvelope(Resource(target.Id));
    else channels_.at(uint16_t(target.Id)).Stop();
  }
}
}  // namespace Impacto::Saekano
