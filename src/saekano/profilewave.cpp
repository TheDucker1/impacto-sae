#include "profilewave.h"

#include "commands.h"

#include <bit>

namespace Impacto::Saekano {
void ProfileWaveAnimation::Begin(int32_t period, uint64_t now) {
  if (period < 0) throw FormatError("negative profile wave period");
  Period = uint32_t(period);
  Epoch = Sample = now;
  Current = 0;
  Started = true;
}
void ProfileWaveAnimation::Update(uint64_t now) {
  if (!Started) throw UnsupportedCommand("profile wave Start was suppressed");
  if (!Period) {
    Epoch = Sample = now;
    Current = 0;
    return;
  }
  int32_t delta = std::bit_cast<int32_t>(uint32_t(now) - uint32_t(Sample));
  // Native recursively restarts against the current clock, then applies its
  // signed overshoot. For target960 the positive recursive delta is below960;
  // unroll that bounded recursion to avoid a deep Windows stack on short
  // periods.
  for (size_t restarts = 0; restarts <= 960; ++restarts) {
    if (delta < 0) {
      Epoch = Sample = now;
      delta = 0;
    }
    const uint64_t sample = Sample + uint32_t(delta);
    const uint64_t deadline = Epoch + Period;
    if (sample < deadline) {
      const auto numerator =
          std::bit_cast<int32_t>(960u * uint32_t(sample - Epoch));
      Current = numerator / int32_t(Period);
      Sample = sample;
      return;
    }
    int32_t overshoot = std::bit_cast<int32_t>(uint32_t(sample - deadline));
    if (overshoot > 0)
      overshoot %= 960;  // Actual native target-value remainder.
    Epoch = Sample = now;
    Current = 0;
    delta = overshoot;
  }
  throw FormatError("profile wave restart exceeds native target bound");
}
}  // namespace Impacto::Saekano
