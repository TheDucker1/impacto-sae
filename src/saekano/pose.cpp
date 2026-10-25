#include "pose.h"

#include "commands.h"
#include "resources.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace Impacto::Saekano {
void CharacterBlink::Initialize(uint32_t clockSeed) {
  GameRandom initial;
  initial.Seed(clockSeed);
  Wait = 0;
  WaitTarget = initial.Range(240, 480);
  Phase = 4;
  Hold = 2;
  Known = true;
}
void CharacterBlink::Update(GameRandom& random, bool visible, int32_t opacity) {
  if (!Known) throw UnsupportedCommand("sprite blink state is missing");
  if (!visible || opacity == 0) return;
  if (Wait < WaitTarget) ++Wait;
  if (Wait != WaitTarget) return;
  if (--Hold >= 0) return;
  if (--Phase < 0) {
    Wait = 0;
    WaitTarget = random.Range(240, 480);
    Phase = random.Range(0, 3) == 0 ? 8 : 4;
  }
  constexpr std::array<int32_t, 9> Holds{4, 3, 2, 2, 4, 3, 2, 2, 4};
  Hold = Holds[size_t(Phase)];
}
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
std::vector<UiSpriteBlit> BuildCurrentCharacterPose(
    const PreparedCharacterPose& pose, const CurrentCharacterPoseDraw& state) {
  if (!state.Visible || !state.Color[3]) return {};
  const auto& g = pose.Geometry;
  if (pose.Pose < 0 || state.Face < 0 || state.Face >= 14 ||
      state.EyePhase < 0 || state.EyePhase > 8 || state.MouthFrame < 0 ||
      state.MouthFrame > 2 || g[2] < 0 || g[3] < 0 || g[6] < 0 || g[7] < 2 ||
      g[10] < 0 || g[11] < 2)
    throw FormatError("invalid current character pose draw inputs");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto sub = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
  };
  const auto mul = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) * uint32_t(b));
  };
  const auto integer = [](float value) {
    if (std::isnan(value)) return int32_t(0);
    if (value >= 2147483648.0f) return INT32_MAX;
    if (value <= -2147483648.0f) return INT32_MIN;
    return int32_t(value);
  };
  const float scale = float(state.ScalePercent) / 100.0f,
              inverse = 1.0f - scale;
  const auto key = [&](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::CharacterPose, pose.Pose,
                             entry};
  };
  const float width = float(g[2]) * scale, height = float(g[3]) * scale;
  const auto bodyWidth = integer(scale > 0 ? width : -width),
             bodyHeight = integer(scale > 0 ? height : -height);
  std::vector<UiSpriteBlit> draws;
  draws.push_back(
      {key(0),
       {sub(state.Position[0], bodyWidth / 2), state.Position[1], bodyWidth,
        bodyHeight},
       {0, 0, scale > 0 ? g[2] : sub(0, g[2]), scale > 0 ? g[3] : sub(0, g[3])},
       state.Color});
  const std::array<int32_t, 9> blinkRows{0, 1, 2, 1, 0, 1, 2, 1, 0};
  const auto eyeSource = add(mul(g[7], blinkRows[size_t(state.EyePhase)]), 1);
  const auto mouthSource =
      add(add(mul(g[7], 3), mul(g[11], 2 - state.MouthFrame)), 1);
  for (size_t part = 0; part < 2; ++part) {
    const auto at = 4 + part * 4;
    const auto x = add(sub(add(state.Position[0], g[at]), g[2] / 2),
                       integer(float(sub(g[2] / 2, g[at])) * inverse));
    const auto y = add(add(state.Position[1], g[at + 1]),
                       integer(float(sub(0, g[at + 1])) * inverse));
    const auto h = sub(g[at + 3], 2);
    draws.push_back(
        {key(uint32_t(state.Face + 1)),
         {x, y, integer(float(g[at + 2]) * scale), integer(float(h) * scale)},
         {0, part == 0 ? eyeSource : mouthSource, g[at + 2], h},
         state.Color});
  }
  return draws;
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
void PollCharacterPoseLoads(CharacterPoseCache& cache,
                            const CharacterPoseDriver& driver) {
  if (!cache.Known) return;
  for (size_t i = 0; i < cache.Slots.size(); ++i)
    if (cache.Slots[i].Pose >= 0 && !cache.Slots[i].Ready && driver.Get(i))
      FinishCharacterPose(cache, cache.Slots[i].Pose, driver);
}
namespace {
CharacterState& PoseActor(SemanticState& state,
                          const CharacterPoseCommand& command) {
  if (!state.CharacterRuntimeState.Known || !state.Effects.Known)
    throw UnsupportedCommand(
        "pose needs known character map and common resource gate");
  RequireCache(state.PoseCache);
  auto found = state.Characters.find(uint16_t(command.Character));
  if (found == state.Characters.end() || !found->second.RuntimeKnown ||
      !found->second.Pose.Known)
    throw UnsupportedCommand(
        "pose needs its Start-created or restored runtime");
  return found->second;
}
void CommitReservedFace(CharacterState& actor) {
  if (actor.Pose.ReservedFace >= 0) actor.Pose.Face = actor.Pose.ReservedFace;
  actor.Pose.ReservedFace = -1;
}
void RequestPose(SemanticState& state, CharacterPoseAnimation& animation,
                 CharacterPoseDriver& driver) {
  if (TouchCharacterPose(state.PoseCache, animation.Resolved)) {
    animation.Awaiting = false;
    animation.Phase = 2;
  } else {
    const auto slot = ReserveCharacterPose(state.PoseCache, animation.Resolved);
    PrepareCharacterPose(state.PoseCache, slot, driver);
    animation.Awaiting = true;
  }
}
void InstallPose(SemanticState& state, CharacterState& actor,
                 const CharacterPoseAnimation& animation,
                 CharacterPoseDriver& driver, int32_t duration, bool force,
                 uint64_t now) {
  const auto slot = TouchCharacterPose(state.PoseCache, animation.Resolved);
  if (!slot) throw UnsupportedCommand("pose install resource was evicted");
  const auto& cached = state.PoseCache.Slots[*slot];
  const auto* prepared = driver.Get(*slot);
  if (!cached.Ready || !cached.Geometry || !prepared ||
      prepared->Pose != animation.Resolved)
    throw UnsupportedCommand("pose install reached an unprepared resource");
  if (!actor.PoseHistoryKnown)
    throw UnsupportedCommand("pose install needs known resource history");
  auto& opacity = actor.Tweens[size_t(CharacterTween::Opacity)];
  auto& previous = actor.Tweens[size_t(CharacterTween::PreviousPose)];
  if (actor.Pose.ResourceSlot) {
    actor.Pose.PreviousGeometry = actor.Pose.Geometry;
    actor.Pose.PreviousResourceSlot = actor.Pose.ResourceSlot;
    previous.Start(opacity.To, 0, duration,
                   now);  // Native R50 target, not R54 sample.
    opacity.Start(0, opacity.To, duration, now);
    actor.Pose.PreviousFace = actor.Pose.Face;
    actor.Pose.CrossfadePoseTag = actor.Pose.Current;
  }
  if (force) {
    previous.Start(0, 0, 0, now);
    opacity.Start(opacity.To, opacity.To, 0, now);
  }
  CommitReservedFace(actor);
  actor.Pose.Geometry = cached.Geometry;
  actor.Pose.ResourceSlot = slot;
  actor.Pose.Ready = true;
  actor.PoseHistory.push_back(uint32_t(animation.Resolved));
}
bool PoseCrossfadeFinished(CharacterState& actor, uint64_t now) {
  auto& opacity = actor.Tweens[size_t(CharacterTween::Opacity)];
  auto& previous = actor.Tweens[size_t(CharacterTween::PreviousPose)];
  opacity.Tick(now);
  previous.Tick(now);
  if (!opacity.Finished() || !previous.Finished()) return false;
  if (actor.Pose.Changed) {
    actor.Pose.PreviousGeometry.reset();
    actor.Pose.PreviousResourceSlot.reset();
    actor.Pose.Changed = false;
  }
  return true;
}
}  // namespace
CharacterPoseCommand CharacterPoseCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  CharacterPoseCommand result;
  result.Route = reader.U32();
  result.Character = reader.U32();
  result.Source = reader.U32();
  result.Value = reader.I32();
  result.Duration = reader.I32();
  result.Lane = reader.U32();
  if (result.Source > 2 || (result.Route && result.Lane >= 5))
    throw FormatError("invalid character pose source/lane");
  if (result.Duration < 0)
    throw UnsupportedCommand(
        "negative signed pose tween duration is unsupported");
  return result;
}
void StartCharacterPose(SemanticState& state, CharacterPoseAnimation& animation,
                        const CharacterPoseCommand& command,
                        CharacterPoseDriver& driver) {
  if (!state.CharacterRuntimeState.Known || !state.PoseCache.Known ||
      !state.Effects.Known)
    throw UnsupportedCommand(
        "pose Start needs known map/cache/common resource state");
  auto& actor = EnsureCharacter(state, uint16_t(command.Character));
  if (!actor.Pose.Known)
    throw UnsupportedCommand("restored sprite pose fields are missing");
  animation = {};
  animation.Started = true;
  animation.Resolved = command.Source == 0 ? command.Value
                       : command.Source == 1
                           ? state.Bank0.at(size_t(command.Value))
                           : state.Bank1.at(size_t(command.Value));
  animation.Requested = animation.Resolved;
  animation.Previous = actor.Pose.Current;
  if (animation.Resolved < 0)
    throw FormatError("negative character pose resource ID");
  if (!command.Route) {
    if (actor.Pose.Current != animation.Resolved) {
      actor.Pose.Current = animation.Resolved;
      actor.Pose.Requested = animation.Requested;
      actor.Pose.Changed = true;
    }
    RequestPose(state, animation, driver);
    // Native645f2 is called with the already-current face and force0: no
    // change.
  } else if (animation.Previous != animation.Resolved) {
    RequestPose(state, animation, driver);
    actor.Pose.Pending = animation.Resolved;
  } else {
    // Native ctor/Start leave awaiting34 undefined and phase1 on this edge.
    // Use the existing phase2 equal-pose handler deterministically instead.
    animation.Phase = 2;
  }
}
bool UpdateCharacterPose(SemanticState& state,
                         CharacterPoseAnimation& animation,
                         const CharacterPoseCommand& command,
                         CharacterPoseDriver& driver, bool gateReady, bool lane,
                         bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand("character pose Start was suppressed");
  if (!lane) {
    if (animation.MainCompleted) return true;
    if (!skipping && command.Route) return animation.MainCompleted = true;
    if (!animation.Seen) {
      animation.Seen = true;
      if (skipping) state.SkipPhase = 2;
      return false;
    }
    if (skipping) state.SkipPhase = 1;
    if (command.Route) return animation.MainCompleted = true;
  }
  auto& actor = PoseActor(state, command);
  if (animation.Awaiting && gateReady) RequestPose(state, animation, driver);
  if (lane && skipping) {
    if (animation.Previous == animation.Resolved) {
      CommitReservedFace(actor);
      animation.Awaiting = false;
      animation.MainCompleted = animation.LaneCompleted = true;
      return true;
    }
    // Native lane-skip installs without a phase/foreground check and does not
    // copy pending338 into current330/requested334. Keep those distinct fields.
    InstallPose(state, actor, animation, driver, 0, true, now);
    animation.Phase = 3;
    if (animation.Previous >= 0)
      TouchCharacterPose(state.PoseCache, animation.Previous);
    if (animation.Previous < 0 || PoseCrossfadeFinished(actor, now))
      animation.Phase = 4;
    if (animation.Previous >= 0) animation.LaneCompleted = true;
    return animation
        .LaneCompleted;  // Flush ignores this flag for a first pose.
  }
  if (animation.Phase == 2 && state.CharacterRuntimeState.Foreground.empty()) {
    if (lane && animation.Previous == animation.Resolved) {
      CommitReservedFace(actor);
      animation.Awaiting = false;
      animation.MainCompleted = animation.LaneCompleted = true;
      return true;
    }
    if (lane) {
      actor.Pose.Current = animation.Resolved;
      actor.Pose.Requested = animation.Requested;
      actor.Pose.Changed = true;
    }
    InstallPose(state, actor, animation, driver,
                skipping ? 0 : command.Duration, skipping, now);
    animation.Phase = 3;
  }
  if (animation.Phase == 3) {
    if (animation.Previous >= 0)
      TouchCharacterPose(state.PoseCache, animation.Previous);
    if (animation.Previous < 0 || PoseCrossfadeFinished(actor, now))
      animation.Phase = 4;
  }
  if (animation.Phase == 4) {
    if (lane)
      animation.LaneCompleted = true;
    else
      animation.MainCompleted = true;
  }
  return lane ? animation.LaneCompleted : animation.MainCompleted;
}
}  // namespace Impacto::Saekano
