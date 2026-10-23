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
    actor.Pose.Known = true;
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
  for (auto& [id, actor] : state.Characters) {
    (void)id;
    if (!actor.RuntimeKnown || !actor.Pose.Known) continue;
    auto& face = actor.PreviousFaceOpacity;
    if (state.SkipMode != 0)
      face.StartUnsigned(face.To, face.To, 0, now);
    else
      face.Tick(now);
  }
}
void RestoreStaticCharacterPoses(SemanticState& state,
                                 const CharacterPoseDriver* driver) {
  RequireCharacters(state);
  if (!state.PoseCache.Known ||
      !state.CharacterRuntimeState.Foreground.empty() ||
      !state.CharacterRuntimeState.Background.empty())
    throw UnsupportedCommand(
        "static character pose restore needs known idle cache/queues");
  auto characters = state.Characters;
  for (auto& [id, actor] : characters) {
    (void)id;
    if (!actor.RuntimeKnown)
      throw UnsupportedCommand("static pose restore needs saved actor fields");
    actor.Pose = {};
    actor.Pose.Known = true;
    actor.Pose.Current = actor.Pose.Requested = actor.SavedFields[11];
    actor.Pose.Face = actor.Pose.PreviousFace = actor.SavedFields[0];
    actor.PreviousFaceOpacity = {};
    actor.PoseHistory.clear();
    actor.PoseHistoryKnown =
        true;  // Reset frees runtimes; load constructs anew.
    if (actor.Pose.Current < 0) continue;
    bool found = false;
    for (size_t slot = 0; slot < state.PoseCache.Slots.size(); ++slot) {
      const auto& cached = state.PoseCache.Slots[slot];
      if (cached.Pose != actor.Pose.Current) continue;
      const auto* prepared = driver ? driver->Get(slot) : nullptr;
      if (!cached.Ready || !cached.Geometry || !prepared ||
          prepared->Pose != cached.Pose ||
          prepared->Geometry != *cached.Geometry)
        throw UnsupportedCommand(
            "static pose restore has not reached FILE_READY");
      actor.Pose.Ready = true;
      actor.Pose.ResourceSlot = slot;
      actor.Pose.Geometry = cached.Geometry;
      actor.PoseHistory.push_back(uint32_t(actor.Pose.Current));
      found = true;
      break;
    }
    if (!found)
      throw UnsupportedCommand(
          "saved character pose is absent from initial cache");
  }
  // Cache order was captured after the native install loop's touches. Attaching
  // the already-installed static state must not apply those touches a second
  // time.
  state.Characters = std::move(characters);
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
CharacterPositionCommand CharacterPositionCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterPositionCommand result;
  result.Route = reader.U32();
  result.Character = reader.U32();
  result.Position = {reader.I32(), reader.I32()};
  result.Lane = reader.U32();
  if (result.Route && result.Lane >= 5)
    throw FormatError("character position lane exceeds engine lane count");
  return result;
}
void StartCharacterPosition(SemanticState& state,
                            const CharacterPositionCommand& command) {
  if (!state.CharacterRuntimeState.Known)
    throw UnsupportedCommand("restored character map is missing for position");
  // Native Start searches with the full word, then inserts a narrowed key.
  // Out-of-domain IDs can overwrite an unrelated runtime; no authored record
  // uses them. Reject that undefined/aliasing behavior rather than emulate it.
  if (command.Character > UINT16_MAX)
    throw FormatError("character position ID exceeds native map key domain");
  EnsureCharacter(state, uint16_t(command.Character));
}
void UpdateCharacterPosition(SemanticState& state,
                             const CharacterPositionCommand& command, bool lane,
                             uint64_t now) {
  if (command.Route && !lane) return;
  if (!state.CharacterRuntimeState.Known)
    throw UnsupportedCommand("restored character map is missing for position");
  auto found = state.Characters.find(uint16_t(command.Character));
  if (found == state.Characters.end() || !found->second.RuntimeKnown)
    throw UnsupportedCommand(
        "character position needs its Start-created runtime");
  auto& actor = found->second;
  for (size_t i = 0; i < 2; ++i) {
    const auto value = command.Position[i];
    actor.Tweens[size_t(CharacterTween::X) + i].Start(value, value, 0, now);
  }
}
CharacterOffsetCommand CharacterOffsetCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterOffsetCommand result;
  result.Route = reader.U32();
  result.Character = reader.U32();
  result.Delta = {reader.I32(), reader.I32()};
  result.Duration = reader.U32();
  result.Background = reader.U32();
  result.Lane = reader.U32();
  if (result.Route && result.Lane >= 5)
    throw FormatError("character offset lane exceeds engine lane count");
  return result;
}
void StartCharacterOffset(SemanticState& state,
                          CharacterOffsetAnimation& animation,
                          const CharacterOffsetCommand& command) {
  RequireCharacters(state);
  // Unlike Position's Start, Offset explicitly narrows before map search.
  EnsureCharacter(state, uint16_t(command.Character));
  animation = {true, false};
}
namespace {
bool UpdateCharacterTranslation(SemanticState& state,
                                CharacterOffsetAnimation& animation,
                                const CharacterOffsetCommand& command,
                                bool lane, bool skipping, uint64_t now,
                                bool relative) {
  if (command.Route && !lane) return true;
  RequireCharacters(state);
  if (!animation.Started)
    throw UnsupportedCommand("character translation Start was suppressed");
  auto found = state.Characters.find(uint16_t(command.Character));
  if (found == state.Characters.end() || !found->second.RuntimeKnown)
    throw UnsupportedCommand(
        "character translation needs its Start-created runtime");
  auto& actor = found->second;
  if (!animation.Queued) {
    for (size_t i = 0; i < 2; ++i) {
      auto& tween = actor.Tweens[size_t(CharacterTween::X) + i];
      const auto target =
          relative ? std::bit_cast<int32_t>(uint32_t(tween.Current) +
                                            uint32_t(command.Delta[i]))
                   : command.Delta[i];
      tween.StartUnsigned(tween.Current, target, command.Duration, now);
    }
    // Move and Offset both use background in either lane route.
    (lane || command.Background ? state.CharacterRuntimeState.Background
                                : state.CharacterRuntimeState.Foreground)
        .push_back(uint16_t(command.Character));
    animation.Queued =
        true;  // ScriptState_Set(...,1,true) commits immediately.
  }
  if (skipping) {
    for (auto channel : {CharacterTween::X, CharacterTween::Y}) {
      auto& tween = actor.Tweens[size_t(channel)];
      tween.Start(tween.To, tween.To, 0, now);
    }
    return true;  // No list removal wait or skip-phase mutation.
  }
  return lane || command.Background ||
         state.CharacterRuntimeState.Foreground.empty();
}
}  // namespace
bool UpdateCharacterOffset(SemanticState& state,
                           CharacterOffsetAnimation& animation,
                           const CharacterOffsetCommand& command, bool lane,
                           bool skipping, uint64_t now) {
  return UpdateCharacterTranslation(state, animation, command, lane, skipping,
                                    now, true);
}
CharacterMoveCommand CharacterMoveCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterMoveCommand result;
  result.Route = reader.U32();
  result.Character = reader.U32();
  result.Target = {reader.I32(), reader.I32()};
  result.Duration = reader.U32();
  result.Background = reader.U32();
  result.Lane = reader.U32();
  if (result.Route && result.Lane >= 5)
    throw FormatError("character move lane exceeds engine lane count");
  return result;
}
void StartCharacterMove(SemanticState& state, CharacterMoveAnimation& animation,
                        const CharacterMoveCommand& command) {
  RequireCharacters(state);
  // Unlike Offset, Move searches the full word before narrowing insertion.
  if (command.Character > UINT16_MAX)
    throw FormatError("character move ID exceeds native map key domain");
  EnsureCharacter(state, uint16_t(command.Character));
  animation = {true, false};
}
bool UpdateCharacterMove(SemanticState& state,
                         CharacterMoveAnimation& animation,
                         const CharacterMoveCommand& command, bool lane,
                         bool skipping, uint64_t now) {
  CharacterOffsetCommand translation{command.Route,      command.Character,
                                     command.Target,     command.Duration,
                                     command.Background, command.Lane};
  return UpdateCharacterTranslation(state, animation, translation, lane,
                                    skipping, now, false);
}
CharacterScaleCommand CharacterScaleCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterScaleCommand result;
  result.Route = reader.U32();
  result.Character = reader.U32();
  result.Percent = reader.I32();
  result.Duration = reader.U32();
  result.Background = reader.U32();
  result.Lane = reader.U32();
  if (result.Route && result.Lane >= 5)
    throw FormatError("character scale lane exceeds engine lane count");
  return result;
}
void StartCharacterScale(SemanticState& state,
                         CharacterScaleAnimation& animation,
                         const CharacterScaleCommand& command) {
  RequireCharacters(state);
  if (command.Character > UINT16_MAX)
    throw FormatError("character scale ID exceeds native map key domain");
  EnsureCharacter(state, uint16_t(command.Character));
  animation = {true, false};
}
bool UpdateCharacterScale(SemanticState& state,
                          CharacterScaleAnimation& animation,
                          const CharacterScaleCommand& command, bool lane,
                          bool skipping, uint64_t now) {
  if (command.Route && !lane) return true;
  RequireCharacters(state);
  if (!animation.Started)
    throw UnsupportedCommand("character scale Start was suppressed");
  auto found = state.Characters.find(uint16_t(command.Character));
  if (found == state.Characters.end() || !found->second.RuntimeKnown)
    throw UnsupportedCommand("character scale needs its Start-created runtime");
  auto& tween = found->second.Tweens[size_t(CharacterTween::Scale)];
  if (!animation.Queued) {
    tween.StartUnsigned(tween.Current, command.Percent, command.Duration, now);
    // Normal lane always uses background. Lane-skip instead honors the
    // authored background policy, unlike Move/Offset's lane-skip handlers.
    const bool background = (lane && !skipping) || command.Background != 0;
    (background ? state.CharacterRuntimeState.Background
                : state.CharacterRuntimeState.Foreground)
        .push_back(uint16_t(command.Character));
    animation.Queued = true;
  }
  if (skipping) {
    tween.StartUnsigned(tween.To, tween.To, 0, now);
    return true;
  }
  return lane || command.Background ||
         state.CharacterRuntimeState.Foreground.empty();
}
CharacterFaceCommand CharacterFaceCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterFaceCommand result;
  result.Route = reader.U32();
  result.Character = reader.U32();
  result.Source = reader.U32();
  result.Value = reader.I32();
  result.Duration = reader.U32();
  result.Lane = reader.U32();
  if (result.Source > 2 || (result.Route && result.Lane >= 5))
    throw FormatError("invalid character face source/lane");
  return result;
}
void StartCharacterFace(SemanticState& state, CharacterFaceAnimation& animation,
                        const CharacterFaceCommand& command) {
  if (!state.CharacterRuntimeState.Known)
    throw UnsupportedCommand("restored character map is missing for face");
  // As with Position, native Start searches the full word before narrowing
  // insertion. Reject out-of-domain alias replacement of an unrelated actor.
  if (command.Character > UINT16_MAX)
    throw FormatError("character face ID exceeds native map key domain");
  animation.Resolved = command.Source == 0 ? command.Value
                       : command.Source == 1
                           ? state.Bank0.at(size_t(command.Value))
                           : state.Bank1.at(size_t(command.Value));
  EnsureCharacter(state, uint16_t(command.Character));
  animation.Started = true;
}
bool UpdateCharacterFace(SemanticState& state,
                         const CharacterFaceAnimation& animation,
                         const CharacterFaceCommand& command, bool lane,
                         bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand("character face Start was suppressed");
  if (command.Route && !lane) return true;
  auto found = state.Characters.find(uint16_t(command.Character));
  if (!state.CharacterRuntimeState.Known || found == state.Characters.end() ||
      !found->second.RuntimeKnown || !found->second.Pose.Known)
    throw UnsupportedCommand("character face needs known initial facial state");
  auto& actor = found->second;
  auto& pose = actor.Pose;
  if (pose.Face != animation.Resolved) {
    if (actor.Tweens[size_t(CharacterTween::Opacity)].Current != 0) {
      pose.PreviousFace = pose.Face;
      actor.PreviousFaceOpacity.StartUnsigned(
          255, 0, skipping ? 0 : command.Duration, now);
    } else {
      pose.PreviousFace = animation.Resolved;
      actor.PreviousFaceOpacity.StartUnsigned(0, 0, 0, now);
    }
    pose.Face = animation.Resolved;
    pose.PreviousFacePose = pose.Current;
  }
  return true;  // Tween208 continues in the per-frame actor pass, outside
                // queues.
}
}  // namespace Impacto::Saekano
