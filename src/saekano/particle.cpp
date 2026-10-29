#include "particle.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

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
uint32_t ParticleDefinition::Bits(size_t nativeOffset) const {
  if (nativeOffset < 0x20 || nativeOffset >= 0x298 || nativeOffset % 4)
    throw FormatError("particle parameter offset outside native record");
  return Parameters[(nativeOffset - 0x20) / 4];
}
ParticleDefinition ParticleDefinition::Decode(std::span<const uint8_t> bytes) {
  ParticleDefinition result;
  PayloadReader reader(bytes);
  const auto put = [&](size_t offset, uint32_t bits) {
    result.Parameters[(offset - 0x20) / 4] = bits;
  };
  const auto read = [&](size_t first, size_t last) {
    for (size_t offset = first; offset <= last; offset += 4)
      put(offset, reader.U32());
  };
  const auto version = [&](size_t section) {
    return result.SectionVersions[section] = reader.I32();
  };
  constexpr uint32_t One = 0x3f800000;
  const auto header = version(0);
  read(0x20, 0x38);
  if (header <= 7) {
    for (auto offset : {0x3c, 0x40, 0x44}) put(offset, 0);
  } else if (header == 8) {
    const auto bits = reader.U32();
    for (auto offset : {0x3c, 0x40, 0x44}) put(offset, bits);
  } else read(0x3c, 0x44);
  version(1); read(0x48, 0x60);
  version(2); read(0x64, 0xa0);
  version(3); read(0xa4, 0xe0);
  version(4); read(0xe4, 0x110);
  const auto section5 = version(5);
  if (section5 >= 27) {
    read(0x114, 0x144);
    put(0x148, section5 >= 28 ? reader.U32() : One);
  } else if (section5 == 19) {
    for (size_t offset = 0x114; offset <= 0x130; offset += 4)
      put(offset, offset == 0x120 || offset == 0x130 ? One : 0);
    read(0x134, 0x140); put(0x144, 0); put(0x148, One);
  } else {
    // Older native branches leave these fields untouched. Their constructor
    // defaults are not yet bound; reject instead of inventing restored values.
    throw FormatError("particle section5 needs unbound legacy defaults");
  }
  read(0x14c, 0x160); read(0x164, 0x180);
  const auto section6 = version(6);
  for (size_t first = 0x184; first <= 0x1b4; first += 16) {
    read(first, first + 8);
    put(first + 12, section6 == 18 ? 0 : reader.U32());
  }
  read(0x1c4, 0x1d8);
  version(7); read(0x1dc, 0x200);
  const auto section8 = version(8);
  read(0x204, 0x230); put(0x234, section8 >= 19 ? reader.U32() : 0);
  if (section8 >= 18) read(0x238, 0x24c);
  else if (section8 == 16) {
    read(0x238, 0x244); put(0x248, One); put(0x24c, One);
  } else if (section8 == 12) {
    for (size_t offset = 0x238; offset <= 0x24c; offset += 4)
      put(offset, offset == 0x240 || offset == 0x248 || offset == 0x24c ? One : 0);
  } else throw FormatError("particle section8 needs unbound legacy defaults");
  const auto section9 = version(9);
  read(0x250, 0x264);
  if (section9 > 16) read(0x268, 0x270);
  else {
    const auto bits = reader.U32();
    for (auto offset : {0x268, 0x26c, 0x270}) put(offset, bits);
  }
  read(0x274, 0x280); put(0x284, section9 == 15 ? One : reader.U32());
  read(0x288, 0x294);
  result.PrefixBytes = bytes.size() - reader.Remaining();
  result.Tail.assign(bytes.begin() + result.PrefixBytes, bytes.end());
  return result;
}
ParticlePointEmitter ParticlePointEmitter::DecodeRow(std::span<const uint8_t> bytes) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false, ended = false;
  for (uint8_t byte : bytes) {
    if (!byte) throw FormatError("NUL in particle CSV row");
    if (ended) {
      if (byte != '\r' && byte != '\n') throw FormatError("multiple particle CSV rows");
    } else if (byte == '"') quoted = !quoted;
    else if (!quoted && (byte == '\r' || byte == '\n')) ended = true;
    else if (!quoted && byte == ',') { fields.push_back(std::move(field)); field.clear(); }
    else field.push_back(char(byte));
  }
  if (quoted) throw FormatError("unterminated particle CSV quote");
  fields.push_back(std::move(field));
  if (!fields.empty() && fields.back().empty()) fields.pop_back();
  if (fields.size() != 16 || fields[0] != "ON" || fields[1] != "ADD_POINT_POINT")
    throw FormatError("unsupported particle point row");
  ParticlePointEmitter result;
  result.ParticleName = fields[2]; result.TextureName = fields[3];
  if (result.ParticleName.empty() || result.TextureName.empty())
    throw FormatError("empty particle point resource name");
  for (size_t i = 0; i < 3; ++i) {
    const auto& text = fields[4 + i];
    char* end = nullptr; errno = 0;
    const auto value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end || errno == ERANGE ||
        value < INT32_MIN || value > INT32_MAX)
      throw FormatError("invalid particle point integer");
    result.Integers[i] = int32_t(value);
  }
  for (size_t i = 0; i < 9; ++i) {
    const auto& text = fields[7 + i];
    char* end = nullptr; errno = 0;
    const auto value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end || errno == ERANGE || !std::isfinite(value) ||
        std::abs(value) > std::numeric_limits<float>::max())
      throw FormatError("invalid particle point float");
    result.Values[i] = float(value);  // Native strtod then VCVT.f32.f64.
  }
  return result;
}
}  // namespace Impacto::Saekano
