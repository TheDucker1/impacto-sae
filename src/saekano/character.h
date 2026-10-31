#pragma once

#include "gauge.h"

#include <map>

namespace Impacto::Saekano {
struct SemanticState;
struct NativeSavedCharacter {
  uint16_t Id = 0;
  // Face,x,y,scale,opacity,priority,opaque270,eye1,eye2,lip1,lip2,pose.
  std::array<int32_t, 12> Fields{};
  uint32_t PackedColor = 0;  // Native save word00BBGGRR, not AARRGGBB.
};
struct NativeCharacterSavePrefix {
  std::vector<NativeSavedCharacter> Records;
  uint32_t DefaultColor = 0;
  size_t EndOffset = 0;
};
NativeCharacterSavePrefix DecodeNativeCharacterSavePrefix(
    std::span<const uint8_t> bytes);

// The native eight-tween shared transition group. Autonomous blink/lip and the
// ninth previous-face tween remain separate from this shared scheduling group.
enum class CharacterTween : size_t {
  Opacity,
  X,
  Y,
  Scale,
  Red,
  Green,
  Blue,
  PreviousPose,
  Count
};
struct CharacterState {
  struct PoseState {
    bool Known = false, Ready = false, Changed = false;
    int32_t Current = -1, Requested = -1;
    std::optional<int32_t> Pending;  // Constructor does not initialize R338.
    int32_t Face = 0, PreviousFace = 0, PreviousFacePose = -1,
            ReservedFace = -1;
    std::optional<size_t> ResourceSlot;
    std::optional<std::array<int32_t, 12>> Geometry;
    std::optional<size_t> PreviousResourceSlot;
    std::optional<std::array<int32_t, 12>> PreviousGeometry;
    std::optional<int32_t>
        CrossfadePoseTag;  // Native R2dc, captured during install.
  } Pose;
  std::vector<uint32_t> PoseHistory;
  uint32_t Priority = 0;
  bool RuntimeKnown = false, PoseHistoryKnown = false;
  std::array<ScriptLinearTween, size_t(CharacterTween::Count)> Tweens;
  // Native ninth tween208, ticked for every actor outside the shared queues.
  // Its initial state is known with fresh/static Pose fields, not CRR1 alone.
  ScriptLinearTween PreviousFaceOpacity;
  std::array<int32_t, 12> SavedFields{};
  std::array<uint8_t, 3> Tint{};  // RGB, native R2fa/2f9/2f8.
};
struct CharacterRuntime {
  bool Known = false;
  uint32_t DefaultColor = 0;  // Native C2030, RGB in low-to-high byte order.
  std::vector<uint16_t> Foreground,
      Background;  // Ordered pointer lists; duplicates allowed.
  // C14510 / manager14518. Reset sets0; a character save prefix alone does
  // not contain this later context field. Never derive it from current actors.
  std::optional<uint32_t> MaximumPriority;
};
CharacterState& EnsureCharacter(SemanticState& state, uint16_t id);
void SetCharacterPriority(SemanticState& state, uint16_t id, uint16_t priority);
// Native7cc98: ascending priority, then ascending map key, capped by C14510.
// Includes hidden actors; visibility belongs to the actor draw callback.
std::vector<uint16_t> SceneCharacterDrawOrder(const SemanticState& state);
void RestoreNativeCharacters(SemanticState& state,
                             const NativeCharacterSavePrefix& saved,
                             uint64_t now);
class CharacterPoseDriver;
struct NativeCharacterPoseRestore {
  bool Started = false, Ready = false;
};
// Fresh cache rebuilt from saved actor pose IDs in ascending map-key order.
// Preparation may be asynchronous; no captured cache/heap objects are needed.
void BeginNativeCharacterPoseRestore(SemanticState& state,
                                     CharacterPoseDriver& driver,
                                     NativeCharacterPoseRestore& restore);
bool PollNativeCharacterPoseRestore(SemanticState& state,
                                     CharacterPoseDriver& driver,
                                     NativeCharacterPoseRestore& restore);
void TickCharacterQueues(SemanticState& state, uint64_t now);
void RestoreStaticCharacterPoses(SemanticState& state,
                                 const CharacterPoseDriver* driver);
struct CharacterColorCommand {
  std::array<uint16_t, 3> Color{};
  uint16_t Duration = 0, Background = 0;
  static CharacterColorCommand Decode(std::span<const uint8_t> payload);
};
void StartCharacterColor(SemanticState& state,
                         const CharacterColorCommand& command, uint64_t now);
bool UpdateCharacterColor(SemanticState& state,
                          const CharacterColorCommand& command, bool skipping,
                          uint64_t now);
struct CharacterPositionCommand {
  uint32_t Route = 0, Character = 0;
  std::array<int32_t, 2> Position{};
  uint32_t Lane = 0;
  static CharacterPositionCommand Decode(std::span<const uint8_t> payload);
};
void StartCharacterPosition(SemanticState& state,
                            const CharacterPositionCommand& command);
void UpdateCharacterPosition(SemanticState& state,
                             const CharacterPositionCommand& command, bool lane,
                             uint64_t now);
struct CharacterOffsetCommand {
  uint32_t Route = 0, Character = 0;
  std::array<int32_t, 2> Delta{};
  uint32_t Duration = 0, Background = 0, Lane = 0;
  static CharacterOffsetCommand Decode(std::span<const uint8_t> payload);
};
struct CharacterOffsetAnimation {
  bool Started = false, Queued = false;
};
struct CharacterMoveCommand {
  uint32_t Route = 0, Character = 0;
  std::array<int32_t, 2> Target{};
  uint32_t Duration = 0, Background = 0, Lane = 0;
  static CharacterMoveCommand Decode(std::span<const uint8_t> payload);
};
struct CharacterDrawCommand {
  uint32_t Route = 0, Character = 0;
  int32_t From = 0, To = 0;
  uint32_t Duration = 0, Background = 0, Lane = 0;
  static CharacterDrawCommand Decode(std::span<const uint8_t> payload);
};
struct CharacterDrawAnimation {
  bool Started = false, Queued = false;
};
void StartCharacterDraw(SemanticState& state, CharacterDrawAnimation& animation,
                        const CharacterDrawCommand& command, uint64_t now);
bool UpdateCharacterDraw(SemanticState& state,
                         CharacterDrawAnimation& animation,
                         const CharacterDrawCommand& command, bool lane,
                         bool skipping, uint64_t now);
using CharacterMoveAnimation = CharacterOffsetAnimation;
void StartCharacterMove(SemanticState& state, CharacterMoveAnimation& animation,
                        const CharacterMoveCommand& command);
bool UpdateCharacterMove(SemanticState& state,
                         CharacterMoveAnimation& animation,
                         const CharacterMoveCommand& command, bool lane,
                         bool skipping, uint64_t now);
void StartCharacterOffset(SemanticState& state,
                          CharacterOffsetAnimation& animation,
                          const CharacterOffsetCommand& command);
bool UpdateCharacterOffset(SemanticState& state,
                           CharacterOffsetAnimation& animation,
                           const CharacterOffsetCommand& command, bool lane,
                           bool skipping, uint64_t now);
struct CharacterFaceCommand {
  uint32_t Route = 0, Character = 0, Source = 0, Duration = 0, Lane = 0;
  int32_t Value = 0;
  static CharacterFaceCommand Decode(std::span<const uint8_t> payload);
};
struct CharacterReserveCommand {
  uint32_t Mode = 0, Character = 0;
  int32_t Face = 0;
  // Both words are consumed by the loader but have no lifecycle consumer.
  std::array<uint32_t, 2> Unused{};
  static CharacterReserveCommand Decode(std::span<const uint8_t> payload);
};
void StartCharacterReserve(SemanticState& state,
                           const CharacterReserveCommand& command);
struct CharacterScaleCommand {
  uint32_t Route = 0, Character = 0, Duration = 0, Background = 0, Lane = 0;
  int32_t Percent = 100;
  static CharacterScaleCommand Decode(std::span<const uint8_t> payload);
};
struct CharacterScaleAnimation {
  bool Started = false, Queued = false;
};
void StartCharacterScale(SemanticState& state,
                         CharacterScaleAnimation& animation,
                         const CharacterScaleCommand& command);
bool UpdateCharacterScale(SemanticState& state,
                          CharacterScaleAnimation& animation,
                          const CharacterScaleCommand& command, bool lane,
                          bool skipping, uint64_t now);
struct CharacterFaceAnimation {
  bool Started = false;
  int32_t Resolved = 0;
};
void StartCharacterFace(SemanticState& state, CharacterFaceAnimation& animation,
                        const CharacterFaceCommand& command);
bool UpdateCharacterFace(SemanticState& state,
                         const CharacterFaceAnimation& animation,
                         const CharacterFaceCommand& command, bool lane,
                         bool skipping, uint64_t now);
}  // namespace Impacto::Saekano
