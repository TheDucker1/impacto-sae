#include "pose.h"

#include "commands.h"
#include "resources.h"

#include <algorithm>
#include <set>

namespace Impacto::Saekano {
namespace {
void RequireCache(const CharacterPoseCache& cache) {
  if (!cache.Known)
    throw UnsupportedCommand("initial character pose cache is missing");
}
size_t Slot(int32_t slot) {
  if (slot < 0 || slot >= 10)
    throw FormatError("invalid character pose cache slot");
  return size_t(slot);
}
}  // namespace
CharacterPoseCache FreshCharacterPoseCache() {
  CharacterPoseCache cache;
  cache.Known = true;
  for (auto& slot : cache.Slots) slot.Geometry = std::array<int32_t, 12>{};
  return cache;
}
std::optional<size_t> TouchCharacterPose(CharacterPoseCache& cache,
                                         int32_t pose) {
  RequireCache(cache);
  for (size_t i = 0; i < cache.Order.size(); ++i) {
    if (cache.Order[i] == -1) continue;
    const auto slot = Slot(cache.Order[i]);
    if (cache.Slots[slot].Pose != pose) continue;
    std::rotate(cache.Order.begin() + i, cache.Order.begin() + i + 1,
                cache.Order.end());
    return slot;  // Native lookup deliberately does not test ready byte4.
  }
  return std::nullopt;
}
bool CharacterPoseReady(const CharacterPoseCache& cache, int32_t pose) {
  RequireCache(cache);
  for (auto index : cache.Order)
    if (index != -1 && cache.Slots[Slot(index)].Pose == pose)
      return cache.Slots[Slot(index)].Ready;
  return false;
}
size_t ReserveCharacterPose(CharacterPoseCache& cache, int32_t pose) {
  RequireCache(cache);
  auto free = std::find_if(cache.Slots.begin(), cache.Slots.end(),
                           [](const auto& slot) { return slot.Pose == -1; });
  const auto slot = free == cache.Slots.end()
                        ? Slot(cache.Order.front())
                        : size_t(free - cache.Slots.begin());
  cache.Slots[slot].Pose = pose;
  cache.Slots[slot].Ready = false;
  // Native reservation keeps previous geometry until its load callback writes
  // it. It rotates all ten pointers, including null placeholders.
  std::rotate(cache.Order.begin(), cache.Order.begin() + 1, cache.Order.end());
  cache.Order.back() = int32_t(slot);
  return slot;
}
void ArchiveCharacterPoses::Prepare(size_t slot, int32_t pose) {
  if (slot >= prepared_.size())
    throw FormatError("invalid pose preparation slot");
  if (pose < 0 || size_t(pose) >= resources_.CharacterPoseCount())
    throw FormatError("character pose ID outside original archive");
  prepared_[slot].reset();
  auto raw = resources_.LoadCharacterPose(size_t(pose));
  PreparedCharacterPose prepared;
  prepared.Pose = pose;
  prepared.Geometry = raw.Geometry;
  for (size_t i = 0; i < prepared.Textures.size(); ++i)
    prepared.Textures[i] = DecodeSceneImage(raw.Textures[i].Data);
  prepared_[slot] = std::move(prepared);
}
const PreparedCharacterPose* ArchiveCharacterPoses::Get(size_t slot) const {
  if (slot >= prepared_.size())
    throw FormatError("invalid pose preparation slot");
  return prepared_[slot] ? &*prepared_[slot] : nullptr;
}
void ArchiveCharacterPoses::Reset() {
  for (auto& slot : prepared_) slot.reset();
}
void PrepareCharacterPose(CharacterPoseCache& cache, size_t slot,
                          CharacterPoseDriver& driver) {
  RequireCache(cache);
  if (slot >= cache.Slots.size())
    throw FormatError("invalid pose preparation slot");
  if (cache.Slots[slot].Ready) return;
  driver.Prepare(slot, cache.Slots[slot].Pose);
}
bool FinishCharacterPose(CharacterPoseCache& cache, int32_t pose,
                         const CharacterPoseDriver& driver) {
  RequireCache(cache);
  // Callback70c6 searches physical slots rather than touching LRU order.
  for (size_t i = 0; i < cache.Slots.size(); ++i) {
    auto& slot = cache.Slots[i];
    if (slot.Pose != pose) continue;
    if (slot.Ready) return true;
    const auto* prepared = driver.Get(i);
    if (!prepared) return false;
    if (prepared->Pose != pose)
      throw FormatError("stale character pose load callback");
    slot.Geometry = prepared->Geometry;
    slot.Ready = true;
    return true;
  }
  return false;
}
CharacterPoseCache DecodeNativePoseCache(std::span<const uint8_t> packet) {
  PayloadReader reader(packet);
  if (reader.U32() != 0)
    throw FormatError("unsupported initial pose cache policy");
  CharacterPoseCache cache;
  cache.Known = true;
  std::set<int32_t> ids, occupied, ordered;
  for (size_t slot = 0; slot < cache.Slots.size(); ++slot) {
    const auto pose = reader.I32();
    if (pose < -1 || (pose >= 0 && !ids.insert(pose).second))
      throw FormatError("invalid/duplicate initial pose resource ID");
    cache.Slots[slot].Pose = pose;
    if (pose >= 0) occupied.insert(int32_t(slot));
  }
  for (auto& slot : cache.Order) {
    slot = reader.I32();
    if (slot < -1 || slot >= 10 || (slot >= 0 && !ordered.insert(slot).second))
      throw FormatError("invalid/duplicate initial pose cache order");
  }
  if (ordered != occupied || reader.Remaining())
    throw FormatError(
        "initial pose cache order/occupancy or boundary mismatch");
  return cache;  // No guessed empty-slot geometry; occupied slots await actual
                 // prep.
}
void PrepareNativePoseCache(CharacterPoseCache& cache,
                            CharacterPoseDriver& driver) {
  RequireCache(cache);
  driver.Reset();
  for (size_t slot = 0; slot < cache.Slots.size(); ++slot) {
    auto& record = cache.Slots[slot];
    if (record.Pose < 0) continue;
    record.Ready = false;
    PrepareCharacterPose(cache, slot, driver);
    if (!FinishCharacterPose(cache, record.Pose, driver))
      throw UnsupportedCommand(
          "initial pose cache preparation has not reached FILE_READY");
  }
}
}  // namespace Impacto::Saekano
