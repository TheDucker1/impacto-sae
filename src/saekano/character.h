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

// The native eight-tween shared transition group. Pose resource geometry,
// autonomous blink/lip and the ninth previous-face tween remain separate work.
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
  std::vector<uint32_t> PoseHistory;
  uint32_t Priority = 0;
  bool RuntimeKnown = false, PoseHistoryKnown = false;
  std::array<ScriptLinearTween, size_t(CharacterTween::Count)> Tweens;
  std::array<int32_t, 12> SavedFields{};
  std::array<uint8_t, 3> Tint{};  // RGB, native R2fa/2f9/2f8.
};
struct CharacterRuntime {
  bool Known = false;
  uint32_t DefaultColor = 0;  // Native C2030, RGB in low-to-high byte order.
  std::vector<uint16_t> Foreground,
      Background;  // Ordered pointer lists; duplicates allowed.
};
CharacterState& EnsureCharacter(SemanticState& state, uint16_t id);
void RestoreNativeCharacters(SemanticState& state,
                             const NativeCharacterSavePrefix& saved,
                             uint64_t now);
void TickCharacterQueues(SemanticState& state, uint64_t now);
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
}  // namespace Impacto::Saekano
