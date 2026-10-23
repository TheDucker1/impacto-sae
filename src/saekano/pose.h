#pragma once

#include "image.h"

namespace Impacto::Saekano {
struct CharacterPoseSlot {
  int32_t Pose = -1;
  bool Ready = false;
  std::optional<std::array<int32_t, 12>> Geometry;
};
struct CharacterPoseCache {
  bool Known = false;
  std::array<CharacterPoseSlot, 10> Slots;
  // Native pointer order, oldest to newest; -1 denotes a null entry.
  std::array<int32_t, 10> Order{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};
CharacterPoseCache FreshCharacterPoseCache();
std::optional<size_t> TouchCharacterPose(CharacterPoseCache& cache,
                                         int32_t pose);
bool CharacterPoseReady(const CharacterPoseCache& cache, int32_t pose);
size_t ReserveCharacterPose(CharacterPoseCache& cache, int32_t pose);

struct PreparedCharacterPose {
  int32_t Pose = -1;
  std::array<int32_t, 12> Geometry{};
  std::array<ImagePixels, 15> Textures;
};
class CharacterPoseDriver {
 public:
  virtual ~CharacterPoseDriver() = default;
  virtual void Prepare(size_t slot, int32_t pose) = 0;
  virtual const PreparedCharacterPose* Get(size_t slot) const = 0;
  virtual void Reset() = 0;
  virtual bool GateReady() const { return true; }
};
// Direct original root6 loading and complete P8 pixel preparation. No SDK/GPU.
// Slots have stable identities, matching the native mutable resource records.
class ArchiveCharacterPoses final : public CharacterPoseDriver {
 public:
  explicit ArchiveCharacterPoses(GameResources& resources)
      : resources_(resources) {}
  void Prepare(size_t slot, int32_t pose) override;
  const PreparedCharacterPose* Get(size_t slot) const override;
  void Reset() override;

 private:
  GameResources& resources_;
  std::array<std::optional<PreparedCharacterPose>, 10> prepared_;
};
// Request and callback are separate: an asynchronous driver may return nullptr
// until parsing/upload preparation has actually finished. Neither bypasses the
// owning VM's common resource-sequence gate.
void PrepareCharacterPose(CharacterPoseCache& cache, size_t slot,
                          CharacterPoseDriver& driver);
bool FinishCharacterPose(CharacterPoseCache& cache, int32_t pose,
                         const CharacterPoseDriver& driver);
// Initial-only ready-cache packet, not arbitrary live texture heap restoration.
CharacterPoseCache DecodeNativePoseCache(std::span<const uint8_t> packet);
void PrepareNativePoseCache(CharacterPoseCache& cache,
                            CharacterPoseDriver& driver);
void PollCharacterPoseLoads(CharacterPoseCache& cache,
                            const CharacterPoseDriver& driver);
struct SemanticState;
struct CharacterPoseCommand {
  uint32_t Route = 0, Character = 0, Source = 0, Lane = 0;
  int32_t Value = 0, Duration = 0;
  static CharacterPoseCommand Decode(std::span<const uint8_t> payload);
};
struct CharacterPoseAnimation {
  bool Started = false, Awaiting = false, Seen = false;
  bool MainCompleted = false, LaneCompleted = false;
  int32_t Phase = 1, Resolved = -1, Requested = -1, Previous = -1;
};
void StartCharacterPose(SemanticState& state, CharacterPoseAnimation& animation,
                        const CharacterPoseCommand& command,
                        CharacterPoseDriver& driver);
bool UpdateCharacterPose(SemanticState& state,
                         CharacterPoseAnimation& animation,
                         const CharacterPoseCommand& command,
                         CharacterPoseDriver& driver, bool gateReady, bool lane,
                         bool skipping, uint64_t now);
}  // namespace Impacto::Saekano
