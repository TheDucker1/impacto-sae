#include "texttiming.h"

#include "commands.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
void CheckFactor(float factor) {
  if (!std::isfinite(factor) || factor < 0)
    throw FormatError("invalid text timing factor");
}
uint32_t Duration(uint32_t duration, float factor) {
  CheckFactor(factor);
  // Match separate VCVT.F32.U32, VMUL.F32 and truncating VCVT.U32.F32.
  // The last instruction saturates; a C++ out-of-range float cast is undefined.
  const float product = float(duration) * factor;
  if (product >= 4294967296.0f) return std::numeric_limits<uint32_t>::max();
  if (product <= 0) return 0;
  return uint32_t(product);
}
}  // namespace
TextTimingEnvironment TextTimingEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < 3) throw FormatError("truncated text timing prefix");
  TextTimingEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  const auto count = reader.U32();
  if (count < 3 || count > 4096)
    throw FormatError("text timing factor count outside supported bounds");
  reader.CheckCount(count, 4);
  result.Factors.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto factor = std::bit_cast<float>(reader.U32());
    CheckFactor(factor);
    result.Factors.push_back(factor);
  }
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
VoiceTextDurations TextTimingEnvironment::ForVoice(uint32_t duration,
                                                   uint8_t option) const {
  if (Factors.size() < 3 || option >= Factors.size())
    throw FormatError("text speed option outside authored table");
  return {Duration(duration, Factors[option]), Duration(duration, Factors[2])};
}
}  // namespace Impacto::Saekano
