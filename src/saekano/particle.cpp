#include "particle.h"
#include "commands.h"
#include "resources.h"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Impacto::Saekano {
namespace {
uint32_t NativeUnsignedWord(double value) {
  if (!(value > 0)) return 0;
  if (value >= double(UINT32_MAX)) return UINT32_MAX;
  return uint32_t(value);  // VCVT.u32 truncates; saturate before C++ conversion.
}
}
uint32_t ParticleNominalLifetime(int32_t authoredTicks) {
  const float product = float(authoredTicks) * std::bit_cast<float>(0x4184cccdu);
  return NativeUnsignedWord(double(product));
}
bool PrepareParticleBirthPrefix(ParticleBirthPrefix& state,
    const ParticleDefinition& definition, int32_t authoredTicks,
    const std::array<float, 14>& creation, GameRandom& random, bool textureReady) {
  if (!textureReady) return false;
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  ParticleBirthPrefix next;
  next.Creation = creation;
  next.NominalMilliseconds = ParticleNominalLifetime(authoredTicks);
  const float sample = random.Normal(field(0x2c), 1.0f);
  volatile float product = field(0x28) * sample;
  const float coefficient = field(0x24) + product;
  const float lifetime = coefficient * float(next.NominalMilliseconds);
  if (!std::isfinite(lifetime)) throw FormatError("nonfinite particle lifetime");
  const auto high = NativeUnsignedWord(double(lifetime) * 0x1p-32);
  const auto low = NativeUnsignedWord(double(lifetime) - double(high) * 0x1p32);
  next.LifetimeMilliseconds = uint64_t(high) << 32 | low;
  if (!next.LifetimeMilliseconds) next.LifetimeMilliseconds = next.NominalMilliseconds;
  next.Position = {field(0x250) + creation[0], creation[1] - field(0x254),
                   field(0x258) + creation[2]};
  state = next;
  return true;
}
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
ParticleRotationSetup PrepareParticleRotation(const ParticleDefinition& definition,
                                              GameRandom& random) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  ParticleRotationSetup result;
  for (size_t axis = 0; axis < 3; ++axis)
    if (definition.Bits(0x3c + 4 * axis) == 1)
      result.InitialAngles[axis] = float(random.Range(0, 360)) * std::bit_cast<float>(0x3c8efa35u);
  for (size_t axis = 0; axis < 3; ++axis) {
    if (definition.Bits(0x144)) {
      const float step = float(random.Range(0, 800) - 400) / 10.0f;
      result.Base[axis] = step * field(0x148);
    } else {
      const size_t base = 0x114 + 16 * axis;
      const float sample = random.Normal(field(base + 8), 1.0f);
      volatile float product = field(base + 4) * sample;
      result.Base[axis] = field(base) + product;
    }
  }
  for (size_t axis = 0; axis < 3; ++axis) {
    const float authored = field(0x120 + 16 * axis);
    const float gain = authored == 0 ? 1.0f : authored;
    const float product = result.Base[axis] * gain;
    result.Scaled[axis] = product * std::bit_cast<float>(0x40c90fdbu);
  }
  return result;
}
namespace {
using ParticleMatrix = std::array<float, 16>;
ParticleMatrix IdentityParticleMatrix() {
  ParticleMatrix result{};
  result[0] = result[5] = result[10] = result[15] = 1;
  return result;
}
float ParticleAddProduct(float sum, float a, float b) {
  volatile float product = a * b;
  return sum + product;  // Native VMLA rounds product before addition.
}
void RotateParticleMatrix(ParticleMatrix& matrix, size_t axis, float angle) {
  if (angle == 0) return;
  const float s = std::sin(angle), c = std::cos(angle);
  auto rotation = IdentityParticleMatrix();
  if (axis == 0) { rotation[5] = rotation[10] = c; rotation[6] = s; rotation[9] = -s; }
  if (axis == 1) { rotation[0] = rotation[10] = c; rotation[2] = -s; rotation[8] = s; }
  if (axis == 2) { rotation[0] = rotation[5] = c; rotation[1] = s; rotation[4] = -s; }
  ParticleMatrix next;
  for (size_t col = 0; col < 4; ++col) for (size_t row = 0; row < 4; ++row) {
    float value = matrix[row + 4] * rotation[col * 4 + 1];
    value = ParticleAddProduct(value, matrix[row], rotation[col * 4]);
    value = ParticleAddProduct(value, matrix[row + 8], rotation[col * 4 + 2]);
    next[row + col * 4] = ParticleAddProduct(value, matrix[row + 12], rotation[col * 4 + 3]);
  }
  matrix = next;
}
std::array<float, 3> TransformParticleVector(const ParticleMatrix& matrix,
                                          const std::array<float, 3>& vector) {
  std::array<float, 4> homogeneous;
  for (size_t row = 0; row < 4; ++row) {
    float value = vector[1] * matrix[row + 4];
    value = ParticleAddProduct(value, vector[0], matrix[row]);
    value = ParticleAddProduct(value, vector[2], matrix[row + 8]);
    homogeneous[row] = value + matrix[row + 12];
  }
  return {homogeneous[0] / homogeneous[3], homogeneous[1] / homogeneous[3],
          homogeneous[2] / homogeneous[3]};
}
}
ParticleMatrixSetup PrepareParticleMatrix(const ParticleDefinition& definition,
    const std::array<float, 14>& creation, GameRandom& random) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  constexpr float Radians = std::bit_cast<float>(0x3c8efa35u);
  ParticleMatrixSetup result;
  const float sample = random.Normal(field(0x6c), 1.0f);
  const float speed = ParticleAddProduct(field(0x64), field(0x68), sample);
  const std::array<float, 3> direction{field(0x25c), field(0x260), field(0x264)};
  const float maximum = std::max({std::abs(direction[0]), std::abs(direction[1]), std::abs(direction[2])});
  if (maximum != 0) for (size_t i = 0; i < 3; ++i) {
    const float product = speed * direction[i];
    result.Vector[i] = product / maximum;
  }
  auto randomMatrix = IdentityParticleMatrix();
  std::array<float, 3> angles;
  for (size_t i = 0; i < 3; ++i) {
    const float radians = float(random.Range(0, 360)) * Radians;
    angles[i] = radians * field(0x268 + i * 4);
  }
  for (size_t i = 0; i < 3; ++i) RotateParticleMatrix(randomMatrix, i, angles[i]);
  result.Vector = TransformParticleVector(randomMatrix, result.Vector);
  result.Matrix = IdentityParticleMatrix();
  const std::array<float, 3> creationAngles{-creation[3], creation[4], -creation[5]};
  for (size_t i = 0; i < 3; ++i) RotateParticleMatrix(result.Matrix, i, creationAngles[i]);
  result.Vector = TransformParticleVector(result.Matrix, result.Vector);
  const float a = float(random.Range(0, 360)) * Radians;
  const float b = float(random.Range(0, 360)) * Radians;
  const float radius = field(0x94), planar = radius * std::cos(b);
  result.Scatter = {planar * std::cos(a), radius * std::sin(b), planar * std::sin(a)};
  const float finalSample = random.Normal(field(0x8c), 1.0f);
  result.Scalar = ParticleAddProduct(field(0x84), field(0x88), finalSample);
  return result;
}
namespace {
int32_t ParticleSignedWord(float value) {
  if (std::isnan(value)) return 0;
  if (double(value) >= double(INT32_MAX)) return INT32_MAX;
  if (double(value) <= double(INT32_MIN)) return INT32_MIN;
  return int32_t(value);
}
}
ParticleColorSetup PrepareParticleColor(const ParticleDefinition& definition,
                                      const std::array<float, 14>& creation) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  const auto integer = [&](size_t offset) { return std::bit_cast<int32_t>(definition.Bits(offset)); };
  volatile float attenuation = std::abs(field(0x1c4)) * .25f;
  const float factor = 1.0f - attenuation;
  const bool negative = field(0x1c4) < 0;
  std::array<int32_t, 3> channels;
  for (size_t axis = 0; axis < 3; ++axis) {
    const auto a = integer(0x184 + axis * 4), b = integer(0x194 + axis * 4);
    const auto scaled = ParticleSignedWord(float(negative ? b : a) * factor);
    const auto sum = std::bit_cast<int32_t>(uint32_t(scaled) + uint32_t(negative ? a : b));
    const float gain = float(integer(0x278 + axis * 4)) / 250.0f;
    const float product = float(sum) * gain;
    channels[axis] = ParticleSignedWord(product * creation[10 + axis]);
  }
  const float alpha = negative ? ParticleAddProduct(field(0x190), field(0x1a0), factor)
                               : ParticleAddProduct(field(0x1a0), field(0x190), factor);
  ParticleColorSetup result;
  result.Intensity = (alpha + 0.0f) * field(0x284);
  const auto maximum = *std::max_element(channels.begin(), channels.end());
  // Native divides by zero for black and VCVT then produces zero. Avoid that
  // invalid arithmetic explicitly, preserving the resulting black color.
  if (maximum != 0) {
    const float gain = 255.0f / float(maximum);
    for (size_t axis = 0; axis < 3; ++axis)
      result.Bgr[2 - axis] = uint8_t(NativeUnsignedWord(double(float(channels[axis]) * gain)));
  }
  return result;
}
bool PrepareParticleBirth(ParticleBirthState& state,
    const ParticleDefinition& definition, int32_t authoredTicks,
    const std::array<float, 14>& creation, GameRandom& random, bool textureReady) {
  if (!textureReady) return false;
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  const auto sample = [&](size_t base, size_t gain, size_t mean) {
    const float normal = random.Normal(field(mean), 1.0f);
    return ParticleAddProduct(field(base), field(gain), normal);
  };
  ParticleBirthState next;
  PrepareParticleBirthPrefix(next.Prefix, definition, authoredTicks, creation, random, true);
  next.Rotation = PrepareParticleRotation(definition, random);
  next.Transform = PrepareParticleMatrix(definition, creation, random);
  const float acceleration = sample(0x74, 0x78, 0x7c) * std::bit_cast<float>(0x3dc8b439u);
  next.Acceleration = TransformParticleVector(next.Transform.Matrix, {0, acceleration, 0});
  next.Color = PrepareParticleColor(definition, creation);
  next.Field58 = field(0x274);
  next.Fields70[0] = sample(0x14c, 0x150, 0x15c);
  next.Fields70[1] = sample(0x154, 0x158, 0x15c);
  next.Fields70[2] = sample(0x164, 0x168, 0x174);
  next.Fields70[3] = sample(0x16c, 0x170, 0x174);
  if (definition.Bits(0x234) == 1) next.Field5c = float(random.Range(0, 100)) / 100.0f;
  next.Fields90[0] = sample(0xb4, 0xb8, 0xbc);
  next.Fields90[1] = sample(0xa4, 0xa8, 0xac);
  next.Flags = {true, true};
  state = next;
  return true;
}
namespace {
float ParticleSubtractProduct(float sum, float a, float b) {
  volatile float product = a * b;
  return sum - product;
}
float ParticleRamp(float progress, float rate) {
  float value;
  if (rate > 0) value = progress * rate;
  else {
    const float start = rate + 1.0f;
    value = ParticleSubtractProduct(start, progress, rate);
    if (value < 0) value = 0;
  }
  if (value > 1) value = 1;
  return value;
}
}
ParticleMotionSample EvaluateParticleMotion(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress,
    const std::array<float, 3>& origin) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  const float reversed = 1.0f - progress;
  const auto envelope = [&](size_t base) {
    const float rate = field(base + 8), ramp = ParticleRamp(progress, rate);
    const float toWeight = rate < 0 ? reversed : ramp;
    const float fromWeight = rate < 0 ? ramp : 1.0f - ramp;
    const float to = field(base + 4) * toWeight;
    return ParticleAddProduct(to, field(base), fromWeight) + 0.0f;
  };
  const float first = envelope(0xe4), second = envelope(0xf0);
  const float third = envelope(0xfc), fourth = envelope(0x108);
  ParticleMotionSample result;
  const float xGain = third * first, yGain = fourth * second;
  result.Vector = {xGain * birth.Transform.Vector[0], yGain * birth.Transform.Vector[1],
                   birth.Transform.Vector[2]};
  // Native constructs u64 via high*2^32+low in double, then rounds to float.
  const float lifetime = float(double(uint32_t(birth.Prefix.LifetimeMilliseconds >> 32)) * 0x1p32 +
                               double(uint32_t(birth.Prefix.LifetimeMilliseconds)));
  const float rampTime = lifetime * ParticleRamp(progress, field(0x70));
  const float scaledTime = rampTime * birth.Prefix.Creation[9];
  const float seconds = scaledTime / 1000.0f;
  result.Position = {ParticleAddProduct(origin[0], result.Vector[0], seconds),
                     ParticleSubtractProduct(origin[1], result.Vector[1], seconds),
                     ParticleAddProduct(origin[2], result.Vector[2], seconds)};
  return result;
}
namespace {
float ParticleClockFloat(uint64_t clock) {
  return float(double(uint32_t(clock >> 32)) * 0x1p32 + double(uint32_t(clock)));
}
float ParticlePhase(uint64_t elapsed, uint64_t lifetime, float frequency) {
  if (!lifetime) throw FormatError("particle phase requires positive lifetime");
  const auto ticks = ParticleSignedWord(ParticleClockFloat(elapsed) * frequency);
  const auto phase = uint64_t(int64_t(ticks)) % lifetime;
  return ParticleClockFloat(phase) / ParticleClockFloat(lifetime);
}
float ParticleTriangle(float phase, float a, float b) {
  float from = a, to = b;
  if (phase >= .5f) { phase = phase - .5f; from = b; to = a; }
  const float fraction = phase * 2.0f, delta = to - from;
  return ParticleAddProduct(from, fraction, delta);
}
float ParticleModulation(float value) {
  if (value == 0) return 1;
  if (value > 0) return 1.0f - value;
  return std::abs(value);
}
}
ParticleGeometrySample EvaluateParticleGeometry(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  ParticleGeometrySample result;
  const float widthRamp = ParticleRamp(progress, field(0x160));
  const float widthTo = birth.Fields70[1] * widthRamp;
  const float width = ParticleAddProduct(widthTo, birth.Fields70[0], 1.0f - widthRamp);
  const float widthPhase = ParticlePhase(birth.Elapsed, birth.Prefix.LifetimeMilliseconds, field(0x20c));
  const float widthPulse = ParticleTriangle(widthPhase, field(0x204), field(0x208));
  const float widthBase = birth.Field58 * width;
  const float widthModulated = widthBase * ParticleModulation(widthPulse);
  const float widthScaled = widthModulated * 6.0f;
  result.FieldDc = widthScaled * birth.Prefix.Creation[9];
  const float scaleRamp = ParticleRamp(progress, field(0x178));
  const float scaleTo = birth.Fields70[3] * scaleRamp;
  const float scale = ParticleAddProduct(scaleTo, birth.Fields70[2], 1.0f - scaleRamp);
  const float scalePhase = ParticlePhase(birth.Elapsed, birth.Prefix.LifetimeMilliseconds, field(0x240));
  const float scalePulse = ParticleTriangle(scalePhase, field(0x238), field(0x23c));
  result.Scale = {ParticleModulation(scalePulse * field(0x248)) * scale,
                  ParticleModulation(scalePulse * field(0x24c)) * scale};
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
namespace {
std::vector<std::string> ParticleFields(std::span<const uint8_t> bytes) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false, ended = false;
  for (size_t i = 0; i < bytes.size(); ++i) {
    const auto byte = bytes[i];
    if (!byte) throw FormatError("NUL in particle CSV row");
    if (ended) {
      if (byte != '\r' && byte != '\n') throw FormatError("multiple particle CSV rows");
    } else if (byte == '"') {
      if (quoted && i + 1 < bytes.size() && bytes[i + 1] == '"') { field.push_back('"'); ++i; }
      else quoted = !quoted;
    }
    else if (!quoted && (byte == '\r' || byte == '\n')) ended = true;
    else if (!quoted && byte == ',') { fields.push_back(std::move(field)); field.clear(); }
    else field.push_back(char(byte));
  }
  if (quoted) throw FormatError("unterminated particle CSV quote");
  fields.push_back(std::move(field));
  if (!fields.empty() && fields.back().empty()) fields.pop_back();
  return fields;
}
int32_t ParticleInteger(const std::string& text) {
  char* end = nullptr; errno = 0;
  const auto value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end || errno == ERANGE || value < INT32_MIN || value > INT32_MAX)
    throw FormatError("invalid particle CSV integer");
  return int32_t(value);
}
float ParticleFloat(const std::string& text) {
  char* end = nullptr; errno = 0;
  const auto value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end || errno == ERANGE || !std::isfinite(value) ||
      std::abs(value) > std::numeric_limits<float>::max())
    throw FormatError("invalid particle CSV float");
  return float(value);
}
}  // namespace
ParticlePointEmitter ParticlePointEmitter::DecodeRow(std::span<const uint8_t> bytes) {
  const auto fields = ParticleFields(bytes);
  if (fields.size() != 16 || fields[0] != "ON" || fields[1] != "ADD_POINT_POINT")
    throw FormatError("unsupported particle point row");
  ParticlePointEmitter result;
  result.ParticleName = fields[2]; result.TextureName = fields[3];
  if (result.ParticleName.empty() || result.TextureName.empty())
    throw FormatError("empty particle point resource name");
  for (size_t i = 0; i < 3; ++i) result.Integers[i] = ParticleInteger(fields[4 + i]);
  for (size_t i = 0; i < 9; ++i) result.Values[i] = ParticleFloat(fields[7 + i]);
  return result;
}
ParticleEffectHeader ParticleEffectHeader::DecodeRow(std::span<const uint8_t> bytes) {
  const auto fields = ParticleFields(bytes);
  if (fields.size() != 6 || fields[0] != "ON" || fields[1] != "EFFECT" || fields[2].empty())
    throw FormatError("unsupported particle effect header");
  return {fields[2], {ParticleInteger(fields[3]), ParticleInteger(fields[4])}, ParticleFloat(fields[5])};
}
std::array<float, 3> ParticlePointEmitter::Origin(int32_t emitted) const {
  if (Integers[2] <= 0 || emitted < 0 || emitted >= Integers[2])
    throw FormatError("particle emission index outside positive count");
  std::array<float, 3> result;
  for (size_t axis = 0; axis < 3; ++axis) {
    const float delta = Values[6 + axis] - Values[3 + axis];
    const float product = delta * float(emitted);
    const float step = product / float(Integers[2]);
    const float offset = Values[3 + axis] + step;
    result[axis] = offset + Values[axis];
  }
  return result;
}
void ParticleEffectCatalog::AppendCsv(std::span<const uint8_t> bytes) {
  size_t start = 0;
  bool quoted = false;
  const auto append = [&](std::span<const uint8_t> row) {
    const auto fields = ParticleFields(row);
    if (fields.empty() || fields[0].empty() || fields[0] != "ON") return;
    if (fields.size() < 2) throw FormatError("missing particle command type");
    if (fields[1] == "EFFECT") {
      auto header = ParticleEffectHeader::DecodeRow(row);
      if (Names.contains(header.Name)) { Active.reset(); return; }
      Active = Programs.size(); Names.emplace(header.Name, *Active);
      Programs.push_back({std::move(header), {}});
    } else if (fields[1] == "ADD_POINT_POINT") {
      auto point = ParticlePointEmitter::DecodeRow(row);
      if (Active) Programs.at(*Active).Points.push_back(std::move(point));
    } else throw FormatError("unbound particle CSV command");
  };
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] == '"') {
      if (quoted && i + 1 < bytes.size() && bytes[i + 1] == '"') ++i;
      else quoted = !quoted;
    } else if (!quoted && bytes[i] == '\n') {
      append(bytes.subspan(start, i + 1 - start)); start = i + 1;
    }
  }
  if (quoted) throw FormatError("unterminated particle CSV quote");
  if (start < bytes.size()) append(bytes.subspan(start));
}
PreparedParticleEffects PreparedParticleEffects::Prepare(const EffectResourceAssets& assets) {
  PreparedParticleEffects result;
  for (const auto& file : assets.Files) {
    std::string name(file.Name.begin(), file.Name.end());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) throw FormatError("particle asset has no extension");
    const auto stem = name.substr(0, dot), extension = name.substr(dot);
    if (extension == ".prtf") {
      if (!result.Definitions.emplace(stem, ParticleDefinition::Decode(file.Data)).second)
        throw FormatError("duplicate particle definition name");
    } else if (extension == ".gxt") {
      if (!result.Textures.emplace(stem, file.Data).second) throw FormatError("duplicate particle texture name");
    } else if (extension == ".csv") result.Catalog.AppendCsv(file.Data);
    else if (extension != ".fls") throw FormatError("unbound particle asset type");
  }
  // Defer linking until every file has loaded; the archive can place CSV first.
  for (const auto& program : result.Catalog.Programs)
    for (const auto& point : program.Points)
      if (!result.Definitions.contains(point.ParticleName) || !result.Textures.contains(point.TextureName))
        throw FormatError("particle point references missing definition or texture");
  return result;
}
}  // namespace Impacto::Saekano
