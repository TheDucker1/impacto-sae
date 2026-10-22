#include "random.h"

#include "commands.h"

#include <bit>
#include <limits>

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
}  // namespace Impacto::Saekano
