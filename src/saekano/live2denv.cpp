#include "live2denv.h"

#include "commands.h"

#include <algorithm>
#include <cmath>

namespace Impacto::Saekano {
Live2dEnvironment Live2dEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3) throw FormatError("truncated Live2D environment flags");
  Live2dEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  result.MouthCycleParts = {reader.I32(), reader.I32()};
  if (result.MouthCycleParts[0] <= 0 || result.MouthCycleParts[1] <= 0)
    throw FormatError("invalid Live2D mouth cycle");
  const auto count = reader.U32();
  reader.CheckCount(count, 8);
  result.Actors.resize(count);
  for (auto& actor : result.Actors) actor.Anchor = {reader.I32(), reader.I32()};
  if (reader.U32() != count)
    throw FormatError("Live2D actor table counts differ");
  reader.CheckCount(count, 16);
  for (auto& actor : result.Actors) {
    actor.BlinkInterval = reader.I32();
    for (auto& duration : actor.BlinkMotion) duration = reader.I32();
    if (actor.BlinkInterval < 0 ||
        std::any_of(
            actor.BlinkMotion.begin(), actor.BlinkMotion.end(),
            [](auto duration) { return duration < 0; }))
      throw FormatError("negative Live2D blink timing");
  }
  if (reader.Remaining()) throw FormatError("trailing Live2D environment data");
  return result;
}
const Live2dActorConfig& Live2dEnvironment::Actor(int32_t logical) const {
  if (logical < 0 || size_t(logical) >= Actors.size())
    throw FormatError("Live2D logical actor configuration is out of range");
  return Actors[size_t(logical)];
}
void Live2dIntegerTween::Start(int32_t from, int32_t to, int32_t duration,
                               uint64_t now, bool enabled) {
  From = Current = from;
  To = to;
  Duration = duration;
  Epoch = Sample = now;
  Enabled = enabled;
}
void Live2dIntegerTween::Tick(uint64_t now) {
  if (!Enabled) return;
  if (now < Sample) Epoch = now;
  Sample = now;
  const auto elapsed = now - Epoch;
  if (Duration <= 0 || elapsed >= uint64_t(Duration))
    Current = To;
  else
    Current = int32_t(int64_t(From) +
                      (int64_t(To) - From) * int64_t(elapsed) / Duration);
}
bool Live2dIntegerTween::Finished() const {
  return Current == To &&
         (Duration <= 0 || Sample - Epoch >= uint64_t(Duration));
}
void Live2dFrameAxis::Target(float to, int32_t transition) {
  if (!std::isfinite(to)) throw FormatError("non-finite Live2D transform");
  const float frames = float(transition) * 60.0f;
  From = Current;
  To = to;
  if (frames < 1000.0f) {
    From = Current = To;
    Step = 0;
  } else
    Step = std::abs(To - From) * 1000.0f / frames;
}
void Live2dFrameAxis::Tick() {
  Current += From > To ? -Step : Step;
  Current = std::clamp(Current, std::min(From, To), std::max(From, To));
}
}  // namespace Impacto::Saekano
