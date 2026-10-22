#include "gauge.h"

#include "commands.h"

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

 private:
  std::span<const uint8_t> bytes_;
  size_t cursor_ = 0;
};
}  // namespace

void ScriptLinearTween::Start(int32_t from, int32_t to, int32_t duration,
                              uint64_t now) {
  if (duration < 0) throw FormatError("negative gauge transition duration");
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
    Current = Add(From, Multiply(delta, int32_t(elapsed)) / Duration);
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
}  // namespace Impacto::Saekano
