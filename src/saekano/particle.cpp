#include "particle.h"
#include "commands.h"
#include <algorithm>
#include <bit>

namespace Impacto::Saekano {
ParticleEnvironment ParticleEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3) throw FormatError("truncated particle environment flags");
  ParticleEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  result.Scalar = std::bit_cast<float>(reader.U32());
  const auto count = reader.U32();
  reader.CheckCount(count, 8);
  result.Effects.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    result.Effects.push_back({reader.String(), reader.I32()});
  if (reader.Remaining()) throw FormatError("trailing particle environment data");
  return result;
}
}  // namespace Impacto::Saekano
