#include "random.h"

#include "commands.h"

#include <bit>
#include <limits>
#include <cmath>

namespace Impacto::Saekano {
void GameRandom::Seed(uint32_t seed) {
  engine_.seed(seed);
  known_ = true;
  seed_ = seed;
  draws_ = 0;
}
uint32_t GameRandom::Word() {
  if (!known_)
    throw UnsupportedCommand("restored game random state is missing");
  ++draws_;
  return uint32_t(engine_());
}
int32_t GameRandom::Range(int32_t lower, int32_t upper) {
  const int64_t width = int64_t(upper) - lower;
  if (width < 0 || width > std::numeric_limits<int32_t>::max())
    throw FormatError("unsupported game random range");
  const auto scaledWidth = double(width) * 0x1p-32;
  const int32_t offset = static_cast<int32_t>(double(Word()) * scaledWidth);
  return std::bit_cast<int32_t>(uint32_t(lower) + uint32_t(offset));
}
float GameRandom::Normal(float mean, float spread) {
  if (!std::isfinite(mean) || !std::isfinite(spread))
    throw FormatError("nonfinite particle normal distribution parameters");
  const double first = (double(Word()) + 0.5) * 0x1p-32;
  const double radius = std::sqrt(-2.0 * std::log(first));
  const double second = (double(Word()) + 0.5) * 0x1p-32;
  // Native uses the float approximation of2*pi promoted to double.
  constexpr double Angle = std::bit_cast<double>(uint64_t(0x401921fb60000000));
  const float sample = float(radius * std::sin(second * Angle));
  // VMLA.f32 rounds its product before addition; do not contract to FMA.
  volatile float product = spread * sample;
  return mean + product;
}
}  // namespace Impacto::Saekano
