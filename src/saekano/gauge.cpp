#include "gauge.h"

#include "commands.h"
#include "uisprite.h"
#include "approachmission.h"
#include "approachselection.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
}
int32_t Negate(int32_t a) { return std::bit_cast<int32_t>(0u - uint32_t(a)); }
int32_t Multiply(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) * uint32_t(b));
}
int32_t Truncate(float value) {
  if (!std::isfinite(value) ||
      double(value) < std::numeric_limits<int32_t>::min() ||
      double(value) > std::numeric_limits<int32_t>::max())
    throw FormatError("battle transform exceeds integer range");
  return static_cast<int32_t>(value);  // vcvt.s32.f32, not roundf.
}
const Live2dRecord* ActorRecord(const SemanticState& state, size_t side) {
  const auto found = std::find_if(
      state.Live2d.Records.begin(), state.Live2d.Records.end(),
      [&](const auto& record) {
        return record.LogicalId == state.Battle.SideCharacters[side];
      });
  return found == state.Live2d.Records.end() ? nullptr : &*found;
}
// These are serialized UI records, not their padded native memory sizes.
// Native reader801c4074: short ID + seven words + packed color =34 bytes.
// reader801c3e7c: rectangle16; reader801c43f8: point8.
class EnvironmentReader {
 public:
  explicit EnvironmentReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  void Skip(size_t bytes) {
    if (bytes > bytes_.size() - cursor_)
      throw FormatError("truncated battle environment");
    cursor_ += bytes;
  }
  int32_t Word() {
    PayloadReader reader(bytes_.subspan(cursor_));
    auto value = reader.I32();
    Skip(4);
    return value;
  }
  UiSpriteDescriptor Sprite() {
    PayloadReader reader(bytes_.subspan(cursor_));
    UiSpriteDescriptor sprite;
    sprite.Resource = std::bit_cast<int16_t>(reader.U16());
    for (auto& word : sprite.Words) word = reader.I32();
    Skip(34);
    return sprite;
  }
  UiSpriteDefinition VisualSprite() {
    const auto raw = Sprite();
    const auto color = uint32_t(raw.Words[7]);
    return {raw.Resource, raw.Words[0], raw.Words[1], raw.Words[2],
            raw.Words[3], raw.Words[4], raw.Words[5], raw.Words[6],
            {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color),
             uint8_t(color >> 24)}};
  }
  void Table(size_t width) {
    const auto count = Count(width);
    Skip(count * width);
  }
  size_t Count(size_t width) {
    const auto count = Word();
    if (count < 0 || size_t(count) > (bytes_.size() - cursor_) / width)
      throw FormatError("invalid battle environment table count");
    return size_t(count);
  }
  size_t Position() const { return cursor_; }

 private:
  std::span<const uint8_t> bytes_;
  size_t cursor_ = 0;
};
}  // namespace

void ScriptLinearTween::Start(int32_t from, int32_t to, int32_t duration,
                              uint64_t now) {
  if (duration < 0) throw FormatError("negative gauge transition duration");
  StartUnsigned(from, to, uint32_t(duration), now);
}
void ScriptLinearTween::StartUnsigned(int32_t from, int32_t to,
                                      uint32_t duration, uint64_t now) {
  From = Current = from;
  To = to;
  Duration = duration;
  Epoch = Sample = now;
}
void ScriptLinearTween::Tick(uint64_t now) {
  if (now < Sample) Epoch = now;
  const auto elapsed = now - Epoch;
  if (Duration == 0 || elapsed >= uint64_t(Duration)) {
    Sample = Epoch + uint64_t(Duration);
    Current = To;
  } else {
    Sample = now;
    const auto delta = Add(To, Negate(From));
    // Native compares the zero-extended 64-bit deadline, but its division
    // helper interprets the low duration word as signed32. Widen division to
    // avoid C++ overflow for INT_MIN/-1, then keep the native low32 result.
    const auto divisor = std::bit_cast<int32_t>(Duration);
    const auto quotient = int64_t(Multiply(delta, int32_t(elapsed))) / divisor;
    Current = Add(From, std::bit_cast<int32_t>(uint32_t(quotient)));
  }
}
bool ScriptLinearTween::Finished() const {
  return Current == To && Sample - Epoch >= uint64_t(Duration);
}

BattleGaugeEnvironment BattleGaugeEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  BattleGaugeEnvironment result;
  reader.Skip(3 + 3 * 4);  // flags and colors
  result.Minimum = reader.Word();
  const auto thresholds = reader.Count(4);
  if (thresholds == 0) throw FormatError("battle threshold table is empty");
  result.NeutralThreshold = reader.Word();
  reader.Skip((thresholds - 1) * 4);
  reader.Skip(34 + 2 * 16 + 4 * 34 + 2 * 4);
  reader.Table(8);
  reader.Table(8);
  reader.Table(4);
  const auto strings = reader.Count(4);
  for (size_t i = 0; i < strings; ++i) reader.Table(2);
  reader.Table(8);
  reader.Skip(4);
  reader.Table(4);
  reader.Skip(4 + 2 * 8 + 34 + 4 + 2 * 8 + 4 + 5 * 34);
  const auto groups = reader.Count(4);
  for (size_t i = 0; i < groups; ++i) reader.Table(8);
  reader.Skip(2 * 34 + 4 + 34 + 3 * 4);
  reader.Table(4);
  reader.Skip(34 + 2 * 8 + 5 * 34 + 4);
  reader.Table(16);
  reader.Table(8);
  reader.Skip(2 * 34 + 2 * 4);
  reader.Table(16);
  reader.Table(16);
  for (size_t i = 0; i < 4; ++i) reader.Table(8);
  reader.Skip(13 * 4);  // preceding scalar settings80261a68..80261a98
  result.Duration = reader.Word();
  result.DisplayPerPoint = reader.Word();
  result.XPerPoint = reader.Word();
  result.ScalePerPoint = std::bit_cast<float>(reader.Word());
  // The rest of env_tukkomi belongs to other UI/effects; not decoded here.
  if (result.Minimum < 0 || result.Minimum > 50 || result.Duration < 0 ||
      !std::isfinite(result.ScalePerPoint))
    throw FormatError("invalid battle gauge environment");
  return result;
}
BattleGaugeCommand BattleGaugeCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  BattleGaugeCommand result{reader.I32(), reader.I32()};
  // The loader consumes exactly eight bytes. Record size governs the walker;
  // native records also contain an unread alignment tail (two bytes here).
  return result;
}

void StartBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                      const BattleGaugeCommand& command,
                      const BattleGaugeEnvironment& environment,
                      Live2dDriver* models, uint64_t now) {
  if (!state.Battle.Known)
    throw UnsupportedCommand("restored battle display/side context is missing");
  if (!state.Live2d.Known)
    throw UnsupportedCommand("restored battle model records are missing");
  if (environment.Minimum < 0 || environment.Minimum > 50 ||
      environment.Duration < 0 || !std::isfinite(environment.ScalePerPoint))
    throw FormatError("invalid battle gauge environment");
  auto target = state.BattleGauge;
  const bool side0 = command.Character == state.Battle.SideCharacters[0];
  auto delta = side0 ? command.Delta : Negate(command.Delta);
  target = Add(target, delta);
  // Intentionally directional. Negative authored requests do not imply a
  // symmetric clamp; both native Start branches only clamp their own end.
  if (side0 && target > 100 - environment.Minimum) {
    delta = Add(delta, Add(100 - environment.Minimum, Negate(target)));
    target = 100 - environment.Minimum;
  } else if (!side0 && target < environment.Minimum) {
    delta = Add(delta, Add(environment.Minimum, Negate(target)));
    target = environment.Minimum;
  }
  BattleGaugeAnimation next;
  next.Started = true;
  next.ActualDelta = delta;
  next.Display.Start(
      state.Battle.Display,
      Add(state.Battle.Display, Multiply(delta, environment.DisplayPerPoint)),
      environment.Duration, now);
  for (size_t side = 0; side < 2; ++side) {
    const auto* record = ActorRecord(state, side);
    if (!record || !record->ResourceId) continue;
    if (!models)
      throw UnsupportedCommand("battle model transform backend is unavailable");
    const auto transform = models->CurrentTransform(*record);
    BattleGaugeAnimation::Actor actor;
    const float x = transform.Position[0];
    const float scale = transform.Scale[0] * 100.0f;
    const float scaleDelta = float(delta) * environment.ScalePerPoint;
    actor.X.Start(Truncate(x),
                  Truncate(x + float(Multiply(delta, environment.XPerPoint))),
                  environment.Duration, now);
    actor.Scale.Start(
        Truncate(scale),
        Truncate(side == 0 ? scale + scaleDelta : scale - scaleDelta),
        environment.Duration, now);
    actor.Y.Start(0, 0, environment.Duration, now);
    actor.BaselineY = Truncate(transform.Position[1]);
    next.Actors[side] = actor;
  }
  // Resolve/validate all inputs before committing Start's logical score change.
  animation = next;
  state.BattleGauge = target;
  const int32_t standing = target < environment.NeutralThreshold   ? 0
                           : target > environment.NeutralThreshold ? 2
                                                                   : 1;
  state.Battle.Standings = {standing, 2 - standing};
}
bool UpdateBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                       Live2dDriver* models, bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand(
        "battle gauge Start was suppressed; animation state is unavailable");
  animation.Display.Tick(now);
  state.Battle.Display = animation.Display.Current;
  if (!skipping) {
    for (size_t side = 0; side < 2; ++side) {
      if (!animation.Actors[side]) continue;
      auto& actor = *animation.Actors[side];
      actor.X.Tick(now);
      actor.Scale.Tick(now);
      actor.Y.Tick(now);
      const auto* record = ActorRecord(state, side);
      if (!record || !record->ResourceId) continue;
      if (!models)
        throw UnsupportedCommand(
            "battle model transform backend is unavailable");
      const float scale = float(actor.Scale.Current) / 100.0f;
      // Guard both sides independently. Native8007f604 dereferences side1 under
      // a side0-only guard; retain intended behavior without that null access.
      models->SetTransform(*record,
                           {{float(actor.X.Current),
                             float(Add(actor.BaselineY, actor.Y.Current))},
                            {scale, scale}},
                           now);
    }
  }
  return animation.Display.Finished();
}

namespace {
ApproachGaugeEnvironment ReadApproachGauge(
    EnvironmentReader& reader, ModeResultVisualEnvironment* visuals = nullptr,
    ApproachMissionEnvironment* mission = nullptr,
    ApproachVisualEnvironment* approachVisuals = nullptr,
    ApproachSelectionEnvironment* selection = nullptr) {
  ApproachGaugeEnvironment result;
  reader.Skip(3 + 3 * 4);
  for (size_t i = 0; i < 6; ++i) {
    const auto sprite = reader.Sprite();
    if (approachVisuals) {
      const auto color=uint32_t(sprite.Words[7]);
      approachVisuals->Sprites[i]={sprite.Resource,sprite.Words[0],sprite.Words[1],
          sprite.Words[2],sprite.Words[3],sprite.Words[4],sprite.Words[5],sprite.Words[6],
          {uint8_t(color>>16),uint8_t(color>>8),uint8_t(color),uint8_t(color>>24)}};
    }
    if(mission && i==1){mission->LeftX=sprite.Words[1];mission->LeftWidth=sprite.Words[5];}
    if(mission && i==2)mission->RightX=sprite.Words[1];
    if (i == 0 && visuals) {
      const auto color = uint32_t(sprite.Words[7]);
      visuals->Banner = {sprite.Resource, sprite.Words[0], sprite.Words[1],
                         sprite.Words[2], sprite.Words[3], sprite.Words[4],
                         sprite.Words[5], sprite.Words[6],
                         {uint8_t(color >> 16), uint8_t(color >> 8),
                          uint8_t(color), uint8_t(color >> 24)}};
    }
    if (i == 3)
      result.ParticleRegion = {sprite.Words[1], sprite.Words[2],
                               sprite.Words[5], sprite.Words[6]};
    if (i == 5) result.ParticleSprite = sprite;
  }
  result.PositiveEffectPosition = {reader.Word(), reader.Word()};
  result.NegativeEffectPosition = {reader.Word(), reader.Word()};
  if (approachVisuals) {
    const auto count=reader.Count(34);
    approachVisuals->CharacterLabels.reserve(count);
    for(size_t i=0;i<count;++i)approachVisuals->CharacterLabels.push_back(reader.VisualSprite());
  } else reader.Table(34);
  reader.Skip(2 * 34 + 8 + 34 + 16);
  reader.Table(34);
  reader.Table(34);
  reader.Table(16);
  reader.Table(8);
  reader.Table(8);
  reader.Skip(4);
  for (size_t i = 0; i < 4; ++i) reader.Table(8);
  reader.Skip(4 * 4 + 2 * 16 + 2 * 4);
  if(selection) {
    selection->ScrollDistance=reader.Word();selection->ScrollDuration=reader.Word();
    if(selection->ScrollDistance<0 || selection->ScrollDuration<0)
      throw FormatError("invalid approach selection scroll configuration");
  } else reader.Skip(2*4);
  if (visuals) {
    visuals->ProgressFill = reader.VisualSprite();  // bc18
    visuals->Frame = reader.VisualSprite();  // bc3c
    visuals->MaskedContent = reader.VisualSprite();  // bc60
    visuals->Mask = reader.VisualSprite();  // bc84
    visuals->TextPosition = {reader.Word(), reader.Word()};  // bca8
    visuals->TextWidth = reader.Word();  // bcb0
    visuals->TextScale = std::bit_cast<float>(uint32_t(reader.Word()));  // bcb4
    visuals->EventIcon = reader.VisualSprite();  // bcb8
    visuals->Outcomes[1] = reader.VisualSprite();  // bcdc
    reader.Skip(34);  // bd00, unused by this draw pass.
    visuals->Outcomes[2] = reader.VisualSprite();  // bd24
    visuals->Outcomes[0] = reader.VisualSprite();  // bd48
    reader.Skip(34);  // bd6c
  } else {
    reader.Skip(4 * 34 + 4 * 4 + 6 * 34);
  }
  result.Duration = reader.Word();
  result.ParticleMinimumDuration = reader.Word();
  result.ParticleMaximumDuration = reader.Word();
  const auto count = reader.Word();
  if (result.Duration < 0 || result.ParticleMinimumDuration <= 0 ||
      result.ParticleMaximumDuration < result.ParticleMinimumDuration ||
      count < 0 || count > 10000 || result.ParticleRegion[2] < 0 ||
      result.ParticleRegion[3] < 0)
    throw FormatError("invalid approach gauge environment");
  result.ParticleCount = size_t(count);
  return result;
}
}  // namespace
ApproachGaugeEnvironment ApproachGaugeEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  return ReadApproachGauge(reader);
}
ApproachVisualEnvironment ApproachVisualEnvironment::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachVisualEnvironment result;
  ReadApproachGauge(reader,nullptr,nullptr,&result);
  return result;
}
ApproachSelectionEnvironment ApproachSelectionEnvironment::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachSelectionEnvironment result;
  ReadApproachGauge(reader,nullptr,nullptr,nullptr,&result);return result;
}
ApproachMissionEnvironment ApproachMissionEnvironment::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachMissionEnvironment result;
  ReadApproachGauge(reader,nullptr,&result);
  result.EntranceAlpha=reader.Word();result.LeftSlide=reader.Word();result.RightSlide=reader.Word();
  result.Hold=reader.Word();result.Exit=reader.Word();result.ExitDistance=reader.Word();
  result.PrefixBytes=reader.Position();
  if(result.LeftWidth<0 || result.EntranceAlpha<0 || result.LeftSlide<0 ||
      result.RightSlide<0 || result.Hold<0 || result.Exit<0)
    throw FormatError("invalid approach mission environment");
  return result;
}
ModeResultEnvironment ModeResultEnvironment::DecodeApproach(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  ReadApproachGauge(reader);
  // Native801c9692..801c9c8c: six prior scalar settings, then nine result
  // fields. Walk the variable gauge tables above instead of byte1289.
  reader.Skip(6 * 4);
  ModeResultEnvironment result;
  result.Progress = reader.Word();
  result.HorizontalSlide = reader.Word();
  result.HorizontalStart = reader.Word();
  result.HorizontalOpacity = reader.Word();
  result.VerticalSlide = reader.Word();
  result.VerticalStart = reader.Word();
  result.VerticalOpacity = reader.Word();
  result.SecondaryProgress = reader.Word();
  result.TextOpacity = reader.Word();
  result.PrefixBytes = reader.Position();
  for (const auto duration :
       {result.Progress, result.HorizontalSlide, result.HorizontalOpacity,
        result.VerticalSlide, result.VerticalOpacity, result.SecondaryProgress,
        result.TextOpacity})
    if (duration < 0) throw FormatError("negative mode result duration");
  return result;
}
ModeResultVisualEnvironment ModeResultVisualEnvironment::DecodeApproach(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  ModeResultVisualEnvironment result;
  ReadApproachGauge(reader, &result);
  reader.Skip((6 + 9) * 4);  // Timings.
  for (auto& position : result.CursorPositions)
    position={reader.Word(),reader.Word()};  // bddc/bde4.
  reader.Skip(4);  // bdec.
  result.OverlayAlpha = reader.Word();  // bdf0
  const auto colors = reader.Count(4);
  for (size_t i = 0; i < colors; ++i) {
    const auto color = uint32_t(reader.Word());
    result.CharacterColors.push_back({uint8_t(color >> 16), uint8_t(color >> 8),
                                      uint8_t(color), uint8_t(color >> 24)});
  }
  result.PrefixBytes = reader.Position();
  return result;
}
ApproachGaugeCommand ApproachGaugeCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  return {reader.I32()};  // Remaining record alignment bytes are not fields.
}
namespace {
void StartParticle(ApproachParticle& particle, GameRandom& random, size_t index,
                   const ApproachGaugeEnvironment& environment, uint64_t now) {
  const auto duration = random.Range(environment.ParticleMinimumDuration,
                                     environment.ParticleMaximumDuration);
  particle.Opacity.Start(0, 100, duration, now);
  const auto width = environment.ParticleRegion[2] / 3;
  const auto height = environment.ParticleRegion[3] / 3;
  const auto x =
      Add(environment.ParticleRegion[0], Multiply(width, int32_t(index % 3)));
  const auto y =
      Add(environment.ParticleRegion[1], Multiply(height, int32_t(index / 3)));
  particle.Position = {float(random.Range(x, Add(x, width))),
                       float(random.Range(y, Add(y, height)))};
}
void TickParticle(ApproachParticle& particle, GameRandom& random, size_t index,
                  const ApproachGaugeEnvironment& environment, uint64_t now) {
  if (!particle.Position)
    throw UnsupportedCommand("approach particle runtime is missing");
  particle.Opacity.Tick(now);
  if (!particle.Opacity.Finished()) return;
  if (particle.Opacity.To != 0)
    particle.Opacity.Start(particle.Opacity.To, particle.Opacity.From,
                           particle.Opacity.Duration, now);
  else
    StartParticle(particle, random, index, environment, now);
}
void ApproachCompletionEvents(SemanticState& state, int32_t delta) {
  if (state.ApproachGauge != 100) return;
  if (delta != 0) state.PendingEvents.push_back({GameEventKind::Sound, 23});
  state.PendingEvents.push_back({GameEventKind::Trophy, 37});
}
}  // namespace
void StartApproachGauge(SemanticState& state, ApproachGaugeAnimation& animation,
                        ApproachGaugeCommand& command,
                        const ApproachGaugeEnvironment& environment,
                        uint64_t now) {
  if (!state.Approach.Known)
    throw UnsupportedCommand("restored approach particle context is missing");
  if (!state.Approach.Particles.empty() && !state.Random.Known())
    throw UnsupportedCommand("restored game random state is missing");
  if (state.Approach.Particles.size() != environment.ParticleCount)
    throw UnsupportedCommand(
        "restored approach particle count differs from native configuration");
  if (state.ApproachGauge < 0 || state.ApproachGauge > 100)
    throw FormatError("initial approach gauge is outside0..100");
  if (environment.Duration < 0 || environment.ParticleMinimumDuration <= 0 ||
      environment.ParticleMaximumDuration <
          environment.ParticleMinimumDuration ||
      environment.ParticleRegion[2] < 0 || environment.ParticleRegion[3] < 0)
    throw FormatError("invalid approach gauge environment");
  for (size_t i = 0; i < state.Approach.Particles.size(); ++i)
    StartParticle(state.Approach.Particles[i], state.Random, i, environment,
                  now);
  animation = {};
  animation.Started = true;
  animation.Initial = state.ApproachGauge;
  animation.Target = int32_t(std::clamp(
      int64_t(animation.Initial) + command.Delta, int64_t(0), int64_t(100)));
  command.Delta = animation.Target - animation.Initial;
  animation.Progress.Start(0, 100, environment.Duration, now);
  state.Approach.Changing = true;
  if (command.Delta != 0) {
    const bool positive = command.Delta > 0;
    state.PendingEvents.push_back({GameEventKind::Effect, positive ? 10 : 11,
                                   positive
                                       ? environment.PositiveEffectPosition
                                       : environment.NegativeEffectPosition});
    state.PendingEvents.push_back({GameEventKind::Sound, positive ? 21 : 22});
  }
}
bool UpdateApproachGauge(SemanticState& state,
                         ApproachGaugeAnimation& animation,
                         const ApproachGaugeCommand& command,
                         const ApproachGaugeEnvironment& environment,
                         bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand(
        "approach gauge Start was suppressed; animation state is unavailable");
  if (animation.Completed) return true;
  if (skipping) {
    // Intended endpoint: native adds the entire delta again to an already
    // interpolated gauge. Snap to the original clamped target instead, so a
    // normal->skip transition cannot overshoot or count progress twice.
    state.ApproachGauge = animation.Target;
    animation.Completed = true;
  } else {
    for (size_t i = 0; i < state.Approach.Particles.size(); ++i)
      TickParticle(state.Approach.Particles[i], state.Random, i, environment,
                   now);
    animation.Progress.Tick(now);
    if (animation.Progress.Finished()) {
      state.ApproachGauge = animation.Target;
      state.Approach.Changing = false;
      animation.Completed = true;
    } else {
      const float scaled =
          float(command.Delta) * float(animation.Progress.Current) / 100.0f;
      state.ApproachGauge = animation.Initial + static_cast<int32_t>(scaled);
    }
  }
  if (animation.Completed) ApproachCompletionEvents(state, command.Delta);
  return animation.Completed;
}
}  // namespace Impacto::Saekano
