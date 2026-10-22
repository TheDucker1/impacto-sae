#include "character.h"

#include "commands.h"
#include "imagesave.h"

#include <algorithm>
#include <bit>
#include <set>

namespace Impacto::Saekano {
namespace {
ScriptLinearTween& Tween(CharacterState& actor, CharacterTween channel) {
  return actor.Tweens[size_t(channel)];
}
void RequireCharacters(const SemanticState& state) {
  if (!state.CharacterRuntimeState.Known)
    throw UnsupportedCommand(
        "restored character map/transition queues are missing");
  for (const auto& [id, actor] : state.Characters) {
    (void)id;
    if (!actor.RuntimeKnown)
      throw UnsupportedCommand(
          "restored character animation fields are missing");
  }
}
void SnapColor(CharacterState& actor, uint64_t now) {
  for (size_t i = 0; i < 3; ++i) {
    auto& tween = actor.Tweens[size_t(CharacterTween::Red) + i];
    tween.Start(tween.To, tween.To, 0, now);
    actor.Tint[i] = uint8_t(tween.To);
  }
}
}  // namespace
CharacterState& EnsureCharacter(SemanticState& state, uint16_t id) {
  auto [found, inserted] = state.Characters.try_emplace(id);
  if (inserted) {
    // 800629f8/80062508 initialize the shared animation group independently of
    //  pose textures. Fresh runtimes are hidden, white, at0,0 with100% scale.
    auto& actor = found->second;
    actor.RuntimeKnown = actor.PoseHistoryKnown = true;
    actor.Tint = {255, 255, 255};
    for (auto& tween : actor.Tweens) tween.Start(0, 0, 0, 0);
    Tween(actor, CharacterTween::Scale).Start(100, 100, 0, 0);
    for (size_t i = 0; i < 3; ++i)
      actor.Tweens[size_t(CharacterTween::Red) + i].Start(255, 255, 0, 0);
    actor.SavedFields[3] = 100;
    actor.SavedFields[11] = -1;
  }
  return found->second;
}
NativeCharacterSavePrefix DecodeNativeCharacterSavePrefix(
    std::span<const uint8_t> bytes) {
  const auto image = DecodeNativeImageSavePrefix(bytes);
  size_t cursor = image.EndOffset;
  auto read = [&](size_t size) {
    if (size > bytes.size() - cursor)
      throw FormatError("truncated native character save prefix");
    uint32_t result = 0;
    for (size_t i = 0; i < size; ++i)
      result |= uint32_t(bytes[cursor++]) << (i * 8);
    return result;
  };
  // Three context words, one flag, a counted u16 array and five context words.
  for (size_t i = 0; i < 3; ++i) read(4);
  read(1);
  const auto shorts = read(4);
  if (shorts > 1000000 || shorts > (bytes.size() - cursor) / 2)
    throw FormatError("invalid native character prefix array count");
  for (uint32_t i = 0; i < shorts; ++i) read(2);
  for (size_t i = 0; i < 5; ++i) read(4);
  const auto count = read(4);
  if (count > 65536 || count > (bytes.size() - cursor) / 55)
    throw FormatError("invalid native character record count");
  NativeCharacterSavePrefix result;
  std::set<uint16_t> ids;
  for (uint32_t i = 0; i < count; ++i) {
    // Native writer always emits present1. Absent rows would describe a patch
    // to an existing map, not a complete FILE_READY reconstruction.
    if (read(1) != 1)
      throw FormatError("native character save row is not present");
    NativeSavedCharacter actor;
    actor.Id = uint16_t(read(2));
    if (!ids.insert(actor.Id).second)
      throw FormatError("duplicate native saved character ID");
    for (auto& value : actor.Fields) value = std::bit_cast<int32_t>(read(4));
    actor.PackedColor = read(4);
    result.Records.push_back(actor);
  }
  result.DefaultColor = read(4);
  result.EndOffset = cursor;
  return result;
}
void RestoreNativeCharacters(SemanticState& state,
                             const NativeCharacterSavePrefix& saved,
                             uint64_t now) {
  std::map<uint16_t, CharacterState> actors;
  for (const auto& record : saved.Records) {
    CharacterState actor;
    actor.RuntimeKnown = true;
    actor.SavedFields = record.Fields;
    actor.Priority = uint32_t(record.Fields[5]);
    constexpr std::array<size_t, 4> Fields{4, 1, 2, 3};
    for (size_t i = 0; i < Fields.size(); ++i) {
      const auto value = record.Fields[Fields[i]];
      actor.Tweens[i].Start(value, value, 0, now);
    }
    Tween(actor, CharacterTween::PreviousPose).Start(0, 0, 0, now);
    for (size_t i = 0; i < 3; ++i) {
      actor.Tint[i] = uint8_t(record.PackedColor >> (i * 8));
      actor.Tweens[size_t(CharacterTween::Red) + i].Start(
          actor.Tint[i], actor.Tint[i], 1, now);
    }
    if (!actors.emplace(record.Id, std::move(actor)).second)
      throw FormatError("duplicate native saved character ID");
  }
  state.Characters = std::move(actors);
  state.CharacterRuntimeState = {true, saved.DefaultColor, {}, {}};
}
void TickCharacterQueues(SemanticState& state, uint64_t now) {
  if (!state.CharacterRuntimeState.Known) return;
  auto tick = [&](std::vector<uint16_t>& queue, bool force) {
    for (auto it = queue.begin(); it != queue.end();) {
      auto found = state.Characters.find(*it);
      if (found == state.Characters.end() || !found->second.RuntimeKnown)
        throw UnsupportedCommand(
            "shared character queue refers to unknown runtime");
      auto& actor = found->second;
      bool finished = true;
      for (auto& tween : actor.Tweens) {
        if (force)
          tween.Start(tween.To, tween.To, 0, now);
        else
          tween.Tick(now);
        finished = finished && tween.Finished();
      }
      for (size_t i = 0; i < 3; ++i)
        actor.Tint[i] =
            uint8_t(actor.Tweens[size_t(CharacterTween::Red) + i].Current);
      if (finished)
        it = queue.erase(it);
      else
        ++it;
    }
  };
  tick(state.CharacterRuntimeState.Foreground,
       state.SkipMode != 0 && state.SkipPhase != 0);
  tick(state.CharacterRuntimeState.Background, state.SkipPhase != 0);
}
CharacterColorCommand CharacterColorCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterColorCommand result;
  for (auto& value : result.Color) value = reader.U16();
  result.Duration = reader.U16();
  result.Background = reader.U16();
  return result;
}
void StartCharacterColor(SemanticState& state,
                         const CharacterColorCommand& command, uint64_t now) {
  RequireCharacters(state);
  for (auto& [id, actor] : state.Characters) {
    for (size_t i = 0; i < 3; ++i) {
      auto& tween = actor.Tweens[size_t(CharacterTween::Red) + i];
      tween.Start(actor.Tint[i], uint8_t(command.Color[i]), command.Duration,
                  now);
    }
    if (command.Duration == 0)
      SnapColor(actor, now);
    else
      (command.Background ? state.CharacterRuntimeState.Background
                          : state.CharacterRuntimeState.Foreground)
          .push_back(id);
  }
  state.CharacterRuntimeState.DefaultColor =
      uint8_t(command.Color[0]) | uint32_t(uint8_t(command.Color[1])) << 8 |
      uint32_t(uint8_t(command.Color[2])) << 16;
}
bool UpdateCharacterColor(SemanticState& state,
                          const CharacterColorCommand& command, bool skipping,
                          uint64_t now) {
  RequireCharacters(state);
  if (skipping) {
    for (auto& [id, actor] : state.Characters) {
      (void)id;
      SnapColor(actor, now);
    }
    return true;  // Native skip does not remove shared list nodes or set skip
                  // phase.
  }
  return command.Duration == 0 || command.Background != 0 ||
         state.CharacterRuntimeState.Foreground.empty();
}
}  // namespace Impacto::Saekano
