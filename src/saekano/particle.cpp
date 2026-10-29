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
std::optional<uint64_t> ParticleGroupClock::Delta(size_t group) const {
  if (group >= Paused.size()) throw FormatError("particle group outside manager");
  if (Paused[group]) return std::nullopt;
  return TickMilliseconds;
}
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
std::array<float, 14> ComposeParticleCreation(const std::array<float, 14>& local,
    const std::array<float, 14>& owner, float headerScalar) {
  std::array<float, 14> result;
  for (size_t i = 0; i < 14; ++i) {
    if (i == 9) {
      const float product = local[i] * owner[i];
      result[i] = product * headerScalar;
    } else result[i] = local[i] + owner[i];
  }
  return result;
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
std::array<float, 3> TransformParticleEmitterPosition(const std::array<float, 3>& position,
    const std::array<float, 3>& pivot, const std::array<float, 3>& angles) {
  const auto multiply = [](const ParticleMatrix& a, const ParticleMatrix& b) {
    ParticleMatrix result;
    for (size_t col = 0; col < 4; ++col) for (size_t row = 0; row < 4; ++row) {
      float value = a[row + 4] * b[col * 4 + 1];
      value = ParticleAddProduct(value, a[row], b[col * 4]);
      value = ParticleAddProduct(value, a[row + 8], b[col * 4 + 2]);
      result[row + 4 * col] = ParticleAddProduct(value, a[row + 12], b[col * 4 + 3]);
    }
    return result;
  };
  auto matrix = IdentityParticleMatrix();
  for (size_t i = 0; i < 3; ++i) matrix[12 + i] = -pivot[i];
  for (size_t axis = 0; axis < 3; ++axis) {
    if (angles[axis] == 0) continue;
    auto rotation = IdentityParticleMatrix();
    const float s = std::sin(angles[axis]), c = std::cos(angles[axis]);
    if (axis == 0) { rotation[5] = rotation[10] = c; rotation[6] = s; rotation[9] = -s; }
    if (axis == 1) { rotation[0] = rotation[10] = c; rotation[2] = -s; rotation[8] = s; }
    if (axis == 2) { rotation[0] = rotation[5] = c; rotation[1] = s; rotation[4] = -s; }
    matrix = multiply(rotation, matrix);
  }
  auto translation = IdentityParticleMatrix();
  for (size_t i = 0; i < 3; ++i) translation[12 + i] = pivot[i];
  matrix = multiply(translation, matrix);
  auto result = TransformParticleVector(matrix, position);
  result[1] = -result[1];
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
ParticleColorSample EvaluateParticleColor(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  const auto integer = [&](size_t offset) { return std::bit_cast<int32_t>(definition.Bits(offset)); };
  const auto add = [](int32_t a, int32_t b) { return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b)); };
  const bool negative = field(0x1c4) < 0;
  volatile float reduction = std::abs(field(0x1c4)) * .25f;
  const float attenuation = 1.0f - reduction;
  const float ramp = ParticleRamp(progress, field(0x1c8)), fromWeight = 1.0f - ramp;
  std::array<int32_t, 3> channels;
  for (size_t axis = 0; axis < 3; ++axis) {
    std::array<int32_t, 2> pairs;
    for (size_t pair = 0; pair < 2; ++pair) {
      const size_t base = 0x184 + pair * 32 + axis * 4;
      float a = float(integer(base)), b = float(integer(base + 16));
      if (negative) b = b * attenuation; else a = a * attenuation;
      const float weight = pair ? ramp : fromWeight;
      const auto sum = add(ParticleSignedWord(a * weight), ParticleSignedWord(b * weight));
      pairs[pair] = negative ? sum : sum / 2;
    }
    channels[axis] = add(pairs[0], pairs[1]);
  }
  const auto maximum = *std::max_element(channels.begin(), channels.end());
  // Native caps infinity to1 for black. Avoid the divide-by-zero directly.
  const float normalization = maximum == 0 ? 1.0f : std::min(255.0f / float(maximum), 1.0f);
  std::array<uint32_t, 3> rgb;
  for (size_t axis = 0; axis < 3; ++axis) {
    const float normalized = float(channels[axis]) * normalization;
    const float gain = float(integer(0x278 + axis * 4)) / 255.0f;
    rgb[axis] = uint16_t(ParticleSignedWord(normalized * gain));
  }
  float a = field(0x190), b = field(0x1a0), c = field(0x1b0), d = field(0x1c0);
  if (negative) { b = b * attenuation; d = d * attenuation; }
  else { a = a * attenuation; c = c * attenuation; }
  const float from = ParticleAddProduct(b * fromWeight, a, fromWeight);
  const float to = ParticleAddProduct(d * ramp, c, ramp);
  const float half = (to + from) * .5f;
  float intensity = half * field(0x284);
  const auto multiply = [&](float gain) {
    intensity = intensity * gain;
    for (auto& channel : rgb) channel = uint8_t(NativeUnsignedWord(double(float(channel) * gain)));
  };
  if (field(0x1d4) > 0 && ramp < field(0x1d4) / 10.0f) multiply(field(0x1cc));
  if (field(0x1d8) > 0 && ramp > 1.0f - field(0x1d8) / 10.0f) multiply(field(0x1d0));
  const float phase = ParticlePhase(birth.Elapsed, birth.Prefix.LifetimeMilliseconds, field(0x21c));
  const float pulse = ParticleTriangle(phase, field(0x214), field(0x218));
  const float modulation = std::max(ParticleModulation(pulse), 0.0f);
  intensity = modulation * intensity;
  std::array<float, 3> creation{birth.Prefix.Creation[10], birth.Prefix.Creation[11], birth.Prefix.Creation[12]};
  const float creationMaximum = *std::max_element(creation.begin(), creation.end());
  if (creationMaximum == 0) creation = {1, 1, 1};
  else for (auto& channel : creation) channel = channel / creationMaximum;
  ParticleColorSample result;
  result.Intensity = intensity > 2 ? 2.0f : intensity;
  result.Bgra[3] = 255;
  for (size_t axis = 0; axis < 3; ++axis) {
    const auto channel = uint16_t(std::min(ParticleSignedWord(float(rgb[axis]) * modulation), 255));
    result.Bgra[2 - axis] = uint8_t(NativeUnsignedWord(double(float(channel) * creation[axis])));
  }
  return result;
}
float EvaluateParticleOpacity(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress, GameRandom& random) {
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition.Bits(offset)); };
  const float ramp = ParticleRamp(progress, field(0x1f0));
  const float firstSample = random.Normal(field(0x1ec), 1.0f);
  const float first = ParticleAddProduct(field(0x1dc), field(0x1e0), firstSample);
  const float secondSample = random.Normal(field(0x1ec), 1.0f);
  const float second = ParticleAddProduct(field(0x1e4), field(0x1e8), secondSample);
  const float blend = ParticleAddProduct(second * ramp, first, 1.0f - ramp);
  const float delta = field(0x1f8) - field(0x1f4);
  const float scalarProduct = delta * ramp;
  const float scalar = field(0x1f4) + scalarProduct;
  const float base = field(0x288) * blend;
  const float scaled = base * scalar;
  const float phase = ParticlePhase(birth.Elapsed, birth.Prefix.LifetimeMilliseconds, field(0x22c));
  const float shifted = phase + birth.Field5c;
  const float fractional = shifted - float(ParticleSignedWord(shifted));
  const float pulse = ParticleTriangle(fractional, field(0x224), field(0x228));
  const float modulated = ParticleModulation(pulse) * scaled;
  const float opacity = modulated * birth.Prefix.Creation[13];
  return opacity > 1 ? 1.0f : opacity;
}
bool AdvanceParticle(ParticleRuntimeState& state, const ParticleDefinition* definition,
                     uint64_t elapsedDelta, GameRandom& random) {
  auto& birth = state.Birth;
  if (!birth.Flags[0]) return false;
  if (!definition) return true;
  birth.Elapsed += elapsedDelta;
  const auto lifetime = birth.Prefix.LifetimeMilliseconds;
  // Avoid native zero-lifetime division/modulo; an empty lifetime expires now.
  if (!lifetime) { birth.Flags[0] = false; return true; }
  const auto field = [&](size_t offset) { return std::bit_cast<float>(definition->Bits(offset)); };
  const float lifetimeFloat = ParticleClockFloat(lifetime);
  const float progress = ParticleClockFloat(birth.Elapsed) / lifetimeFloat;
  state.Progress = progress;
  for (size_t i = 0; i < 3; ++i) {
    const float base = state.AddedAngles[i] + birth.Rotation.InitialAngles[i];
    const float angle = ParticleAddProduct(base, progress, birth.Rotation.Scaled[i]);
    state.Angles[i] = angle + birth.Prefix.Creation[3 + i];
  }
  const std::array<float, 3> origin{birth.Prefix.Position[0] + state.OriginOffset[0],
      birth.Prefix.Position[1] + state.OriginOffset[1], birth.Prefix.Position[2]};
  state.Motion = EvaluateParticleMotion(*definition, birth, progress, origin);
  state.Acceleration = birth.Acceleration;
  const auto& vector = state.Motion.Vector;
  if (vector[0] != 0 || vector[1] != 0 || vector[2] != 0) {
    const float angle = birth.Fields90[1] * ParticleRamp(progress, field(0xb0));
    const float radius = birth.Fields90[0] * ParticleRamp(progress, field(0xc0));
    float sine = radius * std::sin(angle), cosine = radius * std::cos(angle);
    float squared = vector[1] * vector[1];
    squared = ParticleAddProduct(squared, vector[0], vector[0]);
    squared = ParticleAddProduct(squared, vector[2], vector[2]);
    const float length = float(std::sqrt(double(squared)));
    const float y = vector[1] / length, x = vector[0] / length, z = vector[2] / length;
    const float denominator = ParticleSubtractProduct(1.0f, y, y);
    const float u = denominator == 0 ? 1.0f : z / denominator;
    const float v = denominator == 0 ? 0.0f : x / denominator;
    const float zero = denominator * 0.0f;
    float vertical;
    if (z == 0) {
      const float product = denominator * cosine;
      cosine = ParticleAddProduct(zero, y, cosine);
      vertical = -ParticleSubtractProduct(product, y, 0.0f);
    } else {
      const float product = denominator * sine;
      sine = -ParticleSubtractProduct(zero, y, sine);
      vertical = ParticleAddProduct(product, y, 0.0f);
    }
    const float horizontal = -ParticleSubtractProduct(v * sine, u, cosine);
    const float depth = ParticleAddProduct(u * sine, v, cosine);
    state.Motion.Position[0] = state.Motion.Position[0] + horizontal;
    state.Motion.Position[1] = state.Motion.Position[1] + vertical;
    state.Motion.Position[2] = state.Motion.Position[2] + depth;
  }
  const float accelerationTime = (lifetimeFloat * ParticleRamp(progress, field(0x80))) / 1000.0f;
  const float squaredTime = accelerationTime * accelerationTime;
  for (size_t i = 0; i < 3; ++i) {
    const float acceleration = state.Acceleration[i] * squaredTime;
    state.Motion.Position[i] = ParticleAddProduct(state.Motion.Position[i], acceleration, .5f);
  }
  const float phase = ParticleRamp(progress, field(0x90)) * std::bit_cast<float>(0x41490fdbu);
  const float angle = phase * birth.Transform.Scalar;
  const float scatter = ParticleRamp(progress, field(0xa0)) * std::sin(angle);
  for (size_t i = 0; i < 3; ++i) {
    state.Scatter[i] = birth.Transform.Scatter[i] * scatter;
    state.Motion.Position[i] = state.Motion.Position[i] + state.Scatter[i];
  }
  state.Color = EvaluateParticleColor(*definition, birth, progress);
  state.Geometry = EvaluateParticleGeometry(*definition, birth, progress);
  state.Opacity = EvaluateParticleOpacity(*definition, birth, progress, random);
  const auto columns = std::bit_cast<int32_t>(definition->Bits(0x58));
  const auto rows = std::bit_cast<int32_t>(definition->Bits(0x5c));
  const int64_t cells = (int64_t(columns) + 1) * (int64_t(rows) + 1);
  if (columns < 0 || rows < 0 || cells > INT32_MAX)
    throw FormatError("invalid particle frame grid");
  const uint64_t frameDuration = lifetime / uint64_t(cells);
  // A grid with more cells than milliseconds has no nonzero native duration.
  // Keep its first frame rather than entering division by zero.
  if (!frameDuration) state.Frame = 0;
  else {
    int32_t frame;
    if (field(0x60) == 0) frame = std::bit_cast<int32_t>(uint32_t(birth.Elapsed / frameDuration));
    else {
      const float time = ParticleClockFloat(birth.Elapsed) * field(0x60);
      frame = ParticleSignedWord(time / ParticleClockFloat(frameDuration));
    }
    state.Frame = int32_t(int64_t(frame) % (int64_t(columns) + 1));
    if (state.Frame > columns) state.Frame = 0;
  }
  if (birth.Elapsed >= lifetime) birth.Flags[0] = false;
  return true;
}
std::array<float, 14> ParticlePointEmitter::Creation(int32_t emitted,
    const std::array<float, 14>& owner, float headerScalar) const {
  // Native186bc4 defaults, then ADD_POINT_POINT overrides position only in
  // the local14 block. Its from/to triples feed Origin separately.
  std::array<float, 14> local{};
  local[9] = local[13] = 1.0f;
  const std::array<float, 3> pivot{Values[0], Values[1], Values[2]};
  const auto position = TransformParticleEmitterPosition(Origin(emitted), pivot, {0, 0, 0});
  std::copy(position.begin(), position.end(), local.begin());
  return ComposeParticleCreation(local, owner, headerScalar);
}
void ParticleEmitterState::Emit(const ParticlePointEmitter& point,
    const ParticleDefinition& definition, const std::array<float, 14>& owner,
    const ParticleEffectHeader& header, const std::array<float, 2>& originOffset,
    GameRandom& random) {
  const auto creation = point.Creation(Emitted, owner, header.Scalar);
  const auto count = std::bit_cast<int32_t>(definition.Bits(0x290));
  for (int32_t i = 0; i < count; ++i) {
    ParticleRuntimeState particle;
    PrepareParticleBirth(particle.Birth, definition, header.Integers[0], creation, random, true);
    particle.OriginOffset = originOffset;
    Particles.push_back(std::move(particle));
  }
  Emitted = std::bit_cast<int32_t>(uint32_t(Emitted) + 1);
}
std::vector<size_t> DueParticleEmissions(const ParticleEffectProgram& program,
    std::span<const ParticleEmitterState> rows, int32_t counter,
    int32_t startOffset, bool initializing) {
  if (rows.size() != program.Points.size()) throw FormatError("particle schedule row mismatch");
  const auto add = [](int32_t a, int32_t b) { return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b)); };
  std::vector<size_t> result;
  for (size_t row = 0; row < rows.size(); ++row) {
    const auto& command = program.Points[row];
    int32_t emitted = rows[row].Emitted;
    const int32_t start = add(startOffset, command.Integers[0]);
    if (initializing) {
      if (uint32_t(start) > uint32_t(counter)) continue;
      result.push_back(row);
      emitted = add(emitted, 1);
    }
    while (emitted < command.Integers[2]) {
      const auto interval = std::bit_cast<int32_t>(uint32_t(command.Integers[1]) * uint32_t(emitted));
      if (uint32_t(add(start, interval)) > uint32_t(counter)) break;
      result.push_back(row);
      emitted = add(emitted, 1);
    }
  }
  return result;
}
namespace {
void EmitEffectRow(ParticleEffectState& state, const ParticleEffectProgram& program,
    const PreparedParticleEffects& assets, size_t row, GameRandom& random) {
  const auto& point = program.Points[row];
  const auto definition = assets.Definitions.find(point.ParticleName);
  if (definition == assets.Definitions.end() || !assets.Textures.contains(point.TextureName))
    throw FormatError("unresolved particle effect resources");
  state.Rows[row].Emit(point, definition->second, state.Owner, program.Header, state.OriginOffset, random);
}
void AgeEffectRow(ParticleEffectState& state, const ParticleEffectProgram& program,
    const PreparedParticleEffects& assets, size_t row, uint64_t delta, GameRandom& random) {
  const auto definition = assets.Definitions.find(program.Points[row].ParticleName);
  if (definition == assets.Definitions.end()) throw FormatError("unresolved aging definition");
  for (auto& particle : state.Rows[row].Particles)
    if (particle.Birth.Flags[1]) AdvanceParticle(particle, &definition->second, delta, random);
}
}
void ParticleEffectState::SetOriginOffset(const std::array<float, 2>& offset) {
  OriginOffset = offset;
  for (auto& row : Rows)
    for (auto& particle : row.Particles) particle.OriginOffset = OriginOffset;
}
void ParticleEffectState::Start(const ParticleEffectProgram& program,
    const PreparedParticleEffects& assets, GameRandom& random) {
  Rows.clear(); Rows.resize(program.Points.size()); Counter = 0;
  Enabled = Running = Emitting = true;
  for (const auto row : DueParticleEmissions(program, Rows, Counter, StartOffset, true))
    EmitEffectRow(*this, program, assets, row, random);
}
bool ParticleEffectState::Update(const ParticleEffectProgram& program,
    const PreparedParticleEffects& assets, uint64_t elapsedDelta, GameRandom& random) {
  if (!Enabled || !Running) return true;
  if (Rows.size() != program.Points.size()) throw FormatError("particle effect row mismatch");
  Counter = std::bit_cast<int32_t>(uint32_t(Counter) + 1);
  const uint32_t deadline = uint32_t(StartOffset) + uint32_t(program.Header.Integers[0]);
  if (deadline > uint32_t(Counter)) {
    const auto due = Emitting ? DueParticleEmissions(program, Rows, Counter, StartOffset, false) : std::vector<size_t>{};
    size_t event = 0;
    for (size_t row = 0; row < Rows.size(); ++row) {
      while (event < due.size() && due[event] == row) { EmitEffectRow(*this, program, assets, row, random); ++event; }
      AgeEffectRow(*this, program, assets, row, elapsedDelta, random);
    }
    return true;
  }
  bool restarted = false;
  const auto repeatLimit = program.Header.Integers[1];
  if (Repeats != repeatLimit) {
    if (repeatLimit != -1) {
      Repeats = std::bit_cast<int32_t>(uint32_t(Repeats) + 1);
    }
    Start(program, assets, random);  // No nested effects in the supported point catalog.
    restarted = true;  // Native42c6 sets success for both finite and-1 repeats.
  } else Running = false;
  bool result = false;
  if (!Emitting || restarted) {
    if ((deadline << 1) <= uint32_t(Counter)) {
      for (auto& row : Rows) row.Emitted = 0;
      Running = false;
    }
    result = true;
  } else Emitting = false;
  for (size_t row = 0; row < Rows.size(); ++row)
    AgeEffectRow(*this, program, assets, row, elapsedDelta, random);
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
ConfiguredParticleEffect ConfiguredParticleEffect::Load(const GameResources& resources,
    const ParticleEnvironment& environment, size_t effectId) {
  if (effectId >= environment.Effects.size()) throw FormatError("particle environment ID outside configuration");
  std::string requested;
  for (const auto code : environment.Effects[effectId].Name) {
    // Native name conversion uses its narrow string view. Current asset keys
    // are ASCII; reject unbound encoding rather than silently narrowing UTF16.
    if (code == 0 || code > 127) throw FormatError("unbound particle effect name encoding");
    requested.push_back(char(code));
  }
  for (size_t archive = 0; archive < resources.EffectCount(); ++archive) {
    auto raw = resources.LoadEffect(archive);
    bool found = false;
    for (const auto& file : raw.Files) {
      std::string filename(file.Name.begin(), file.Name.end());
      while (!filename.empty() && filename.back() == ' ') filename.pop_back();
      if (!filename.ends_with(".csv")) continue;
      const auto inspect = [&](std::span<const uint8_t> row) {
        const auto fields = ParticleFields(row);
        if (fields.size() >= 3 && fields[0] == "ON" && fields[1] == "EFFECT" && fields[2] == requested) {
          ParticleEffectHeader::DecodeRow(row); found = true;
        }
      };
      size_t start = 0; bool quoted = false;
      for (size_t i = 0; i < file.Data.size(); ++i) {
        if (file.Data[i] == '"') {
          if (quoted && i + 1 < file.Data.size() && file.Data[i + 1] == '"') ++i;
          else quoted = !quoted;
        } else if (!quoted && file.Data[i] == '\n') {
          inspect(std::span(file.Data).subspan(start, i + 1 - start)); start = i + 1;
        }
      }
      if (quoted) throw FormatError("unterminated particle CSV quote");
      if (start < file.Data.size()) inspect(std::span(file.Data).subspan(start));
    }
    if (found) {
      auto assets = PreparedParticleEffects::Prepare(raw);
      const size_t program = assets.Catalog.Names.at(requested);
      return {archive, program, std::move(assets)};
    }
  }
  throw FormatError("configured particle effect name not found");
}
ParticleManager::ParticleManager(uint64_t nativeClock) {
  random_.Initialize(nativeClock);
  for (auto& group : groups_)
    for (auto& handle : group)
      handle.State.Enabled = handle.State.Running = handle.State.Emitting = false;
}
void ParticleManager::LoadEffect(const GameResources& resources,
    const ParticleEnvironment& environment, size_t effectId) {
  if (!HasEffect(effectId)) RegisterEffect(effectId, ConfiguredParticleEffect::Load(resources, environment, effectId));
}
void ParticleManager::RegisterEffect(size_t effectId, ConfiguredParticleEffect effect) {
  effect.Program();  // Validate the owned program index before installing it.
  effects_.try_emplace(effectId, std::move(effect));
}
bool ParticleManager::Request(size_t effectId, const ParticleEffectRequest& request, int32_t prewarmMilliseconds) {
  if (request.Group >= GroupCount) throw FormatError("particle request group outside manager");
  const auto effect = effects_.find(effectId);
  if (effect == effects_.end()) return false;
  for (auto& handle : groups_[request.Group]) {
    auto& state = handle.State;
    if (state.Running) continue;
    // Native reset clears row storage/flags, but retains repeat index80.
    state.Rows.clear(); state.Enabled = state.Running = state.Emitting = false;
    handle.EffectId = effectId;
    state.StartOffset = request.StartOffset; state.Owner = request.Owner;
    state.Start(effect->second.Program(), effect->second.Assets, random_.Generator);
    state.SetOriginOffset(origins_[request.Group]);  // Native applies this AFTER Start.
    break;
  }
  // Negative prewarm is invalid authored input; avoid native sign-extension
  // followed by unsigned comparison, which would attempt nearly2^64 ms.
  for (int64_t elapsed = 0; elapsed < prewarmMilliseconds; elapsed += ParticleGroupClock::TickMilliseconds)
    Update();
  return true;
}
void ParticleManager::Update() {
  for (size_t group = 0; group < GroupCount; ++group) {
    const auto delta = clock_.Delta(group);
    if (!delta) continue;
    for (auto& handle : groups_[group]) {
      if (!handle.EffectId) continue;
      const auto& effect = effects_.at(*handle.EffectId);
      // Supported catalogs have no nested effects, so native's nested restart
      // fallback cannot apply. EffectState owns ordinary point repeat behavior.
      handle.State.Update(effect.Program(), effect.Assets, *delta, random_.Generator);
    }
  }
}
void ParticleManager::SetPaused(size_t group, bool paused) {
  if (group >= GroupCount) throw FormatError("particle pause group outside manager");
  clock_.Paused[group] = paused;
}
void ParticleManager::SetOriginOffset(size_t group, const std::array<float, 2>& offset) {
  if (group >= GroupCount) throw FormatError("particle origin group outside manager");
  origins_[group] = offset;
  for (auto& handle : groups_[group]) handle.State.SetOriginOffset(offset);
}
const ParticleManager::GroupHandles& ParticleManager::Handles(size_t group) const {
  if (group >= GroupCount) throw FormatError("particle handle group outside manager");
  return groups_[group];
}
}  // namespace Impacto::Saekano
