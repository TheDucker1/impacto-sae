#include "profilemouth.h"

#include "commands.h"

#include <bit>
#include <limits>

namespace Impacto::Saekano {
void ProfileMouthPlayback::Begin(const ProfileMouthAnimation& envelope,
                                 uint64_t now) {
  if (envelope.Samples.size() > 1024 * 1024)
    throw FormatError("profile mouth samples exceed budget");
  Samples = envelope.Samples;
  Epoch = now;
  Active = Armed = true;
  // Native Start preserves the existing frame/tween until the next update.
}
void ProfileMouthPlayback::Stop() { Active = Armed = false; }
void ProfileMouthPlayback::Constant(int32_t frame) {
  From = To = Frame = frame;
  CycleStart = CycleEnd = 0;
}
void ProfileMouthPlayback::Update(uint64_t now) {
  if (!Active || now < Epoch) {
    Constant(std::bit_cast<int32_t>(2u - uint32_t(Baseline)));
    return;
  }
  const uint32_t index = uint32_t((now - Epoch) / 85);
  if (index >= Samples.size()) {
    Constant(std::bit_cast<int32_t>(2u - uint32_t(Baseline)));
    Active = false;
    return;
  }
  if (!Samples[index]) {
    Armed = true;
    Constant(2);
    return;
  }
  if (Armed) {
    From = Frame = 0;
    To = 3;
    CycleStart = now;
    CycleEnd = now + Cycle;
    Armed = false;
  } else if (now >= CycleEnd) {
    const auto duration = CycleEnd - CycleStart;
    CycleStart = now;
    CycleEnd = now + duration;
    Frame = From;
  } else {
    const auto numerator = std::bit_cast<int32_t>(
        (uint32_t(To) - uint32_t(From)) * uint32_t(now - CycleStart));
    const auto denominator =
        std::bit_cast<int32_t>(uint32_t(CycleEnd - CycleStart));
    if (!denominator ||
        (numerator == std::numeric_limits<int32_t>::min() && denominator == -1))
      throw FormatError("invalid profile mouth cycle division");
    Frame = std::bit_cast<int32_t>(uint32_t(From) +
                                   uint32_t(numerator / denominator));
  }
}
}  // namespace Impacto::Saekano
