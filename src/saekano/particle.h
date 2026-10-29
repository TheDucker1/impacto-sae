#pragma once

#include "formats.h"
#include "random.h"
#include <array>
#include <string>
#include <map>
#include <optional>

namespace Impacto::Saekano {
struct ParticleEffectConfiguration {
  std::u16string Name;
  int32_t Parameter = 0;  // Native60-byte record+38; consumer names it later.
};
struct ParticleEnvironment {
  std::array<uint8_t, 3> Flags{};
  float Scalar = 0;  // Native8025ed70; retain without guessing timing units.
  std::vector<ParticleEffectConfiguration> Effects;
  // Native801d45b8: flags, float, counted UTF16/word records.
  static ParticleEnvironment Decode(std::span<const uint8_t> bytes);
};
struct ParticleDefinition {
  // Native80183390 fills offsets20..294 of a298-byte parameter object. Keep
  // exact bits until individual simulation/draw consumers establish field names.
  std::array<uint32_t, 158> Parameters{};
  std::array<int32_t, 10> SectionVersions{};
  size_t PrefixBytes = 0;
  Bytes Tail;  // Native reader leaves the authored file suffix unread.
  uint32_t Bits(size_t nativeOffset) const;
  static ParticleDefinition Decode(std::span<const uint8_t> bytes);
};
struct ParticleRandomState {
  GameRandom Generator;  // Separate from script/gauge RNG; no implicit seed.
  void Initialize(uint64_t nativeClock) { Generator.Seed(uint32_t(nativeClock)); }
};
struct ParticleBirthPrefix {
  std::array<float, 14> Creation{};  // Native instance14c..180.
  std::array<float, 3> Position{};   // Native instance8..10.
  uint32_t NominalMilliseconds = 0;
  uint64_t LifetimeMilliseconds = 0;  // Native instance f8/fc.
};
// Native1660 tail2fcc..30ba: compose an already transformed local block with
// effect-owner34..68. Header scalar multiplies scale slot9 after the owner.
std::array<float, 14> ComposeParticleCreation(const std::array<float, 14>& local,
    const std::array<float, 14>& owner, float headerScalar);
uint32_t ParticleNominalLifetime(int32_t authoredTicks);
// The native birth prefix before rotation/velocity/scale setup. Missing texture
// returns false without changing existing state or consuming random words.
bool PrepareParticleBirthPrefix(ParticleBirthPrefix& state,
    const ParticleDefinition& definition, int32_t authoredTicks,
    const std::array<float, 14>& creation, GameRandom& random, bool textureReady);
struct ParticleRotationSetup {
  std::array<float, 3> InitialAngles{}, Base{}, Scaled{};  // Native64/38/84 triples.
};
ParticleRotationSetup PrepareParticleRotation(const ParticleDefinition& definition,
                                              GameRandom& random);
struct ParticleMatrixSetup {
  std::array<float, 16> Matrix{};  // Native10c, column major.
  std::array<float, 3> Vector{}, Scatter{};  // Native14/2c.
  float Scalar = 0;  // Native60; later consumers establish its units.
};
ParticleMatrixSetup PrepareParticleMatrix(const ParticleDefinition& definition,
    const std::array<float, 14>& creation, GameRandom& random);
struct ParticleColorSetup {
  std::array<uint8_t, 3> Bgr{};  // Native50..52; byte53 is not written.
  float Intensity = 0;  // Native54, independent of byte color normalization.
};
ParticleColorSetup PrepareParticleColor(const ParticleDefinition& definition,
                                      const std::array<float, 14>& creation);
struct ParticleBirthState {
  ParticleBirthPrefix Prefix;
  ParticleRotationSetup Rotation;
  ParticleMatrixSetup Transform;
  std::array<float, 3> Acceleration{};  // Native44..4c.
  ParticleColorSetup Color;
  float Field58 = 0, Field5c = 0;
  std::array<float, 4> Fields70{};
  std::array<float, 2> Fields90{};
  uint64_t Elapsed = 0;  // Nativef0/f4, initialized here; aging consumer follows.
  std::array<uint32_t, 4> Counters{};  // Nativee8/104/184/188.
  std::array<bool, 2> Flags{};  // Native100/101.
};
// Missing texture preserves state and RNG, as in800a7e66. Resource handles stay
// with the owner; this value contains the state initialized by successful birth.
bool PrepareParticleBirth(ParticleBirthState& state,
    const ParticleDefinition& definition, int32_t authoredTicks,
    const std::array<float, 14>& creation, GameRandom& random, bool textureReady);
struct ParticleMotionSample {
  std::array<float, 3> Position{}, Vector{};  // Native98/a4 triples.
};
// Native8f7e operates on position prepared by the owning age update. Motion is
// evaluated from normalized age and total lifetime, not accumulated per frame.
ParticleMotionSample EvaluateParticleMotion(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress,
    const std::array<float, 3>& origin);
struct ParticleGeometrySample {
  float FieldDc = 0;  // Native8598; renderer will establish its units.
  std::array<float, 2> Scale{};  // Native83a8 -> e0/e4.
};
ParticleGeometrySample EvaluateParticleGeometry(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress);
struct ParticleColorSample {
  std::array<uint8_t, 4> Bgra{};  // Natived4..d7; alpha byte always255.
  float Intensity = 0;  // Natived8, upper cap2.
};
ParticleColorSample EvaluateParticleColor(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress);
float EvaluateParticleOpacity(const ParticleDefinition& definition,
    const ParticleBirthState& birth, float progress, GameRandom& random);
struct ParticleRuntimeState {
  ParticleBirthState Birth;
  std::array<float, 3> AddedAngles{}, Angles{}, Acceleration{}, Scatter{};
  std::array<float, 2> OriginOffset{};  // Native184/188, interpreted as float.
  ParticleMotionSample Motion;
  ParticleColorSample Color;
  ParticleGeometrySample Geometry;
  float Progress = 0, Opacity = 0;
  int32_t Frame = 0;
};
// Native92b6: active-byte gate, elapsed update, all consumers, frame and retirement.
// A missing definition preserves active state; inactive returns false unchanged.
bool AdvanceParticle(ParticleRuntimeState& state, const ParticleDefinition* definition,
                     uint64_t elapsedDelta, GameRandom& random);
struct ParticlePointEmitter {
  // Native ADD_POINT_POINT callback8000ef4c, command type5. Names are raw
  // narrow bytes. Consumer meanings/timing units of the numeric fields follow.
  std::string ParticleName, TextureName;
  std::array<int32_t, 3> Integers{};
  std::array<float, 9> Values{};
  static ParticlePointEmitter DecodeRow(std::span<const uint8_t> bytes);
  // Native creation type5: base + from + (to-from)*emitted/count, f32 stages.
  std::array<float, 3> Origin(int32_t emitted) const;
};
struct ParticleEffectHeader {
  std::string Name;
  std::array<int32_t, 2> Integers{};
  float Scalar = 0;
  static ParticleEffectHeader DecodeRow(std::span<const uint8_t> bytes);
};
struct ParticleEffectProgram {
  ParticleEffectHeader Header;
  std::vector<ParticlePointEmitter> Points;
};
struct ParticleEffectCatalog {
  std::vector<ParticleEffectProgram> Programs;
  std::map<std::string, size_t> Names;
  std::optional<size_t> Active;
  // Native new-name append; duplicate header clears the subsequent-row gate.
  void AppendCsv(std::span<const uint8_t> bytes);
};
struct EffectResourceAssets;
struct PreparedParticleEffects {
  ParticleEffectCatalog Catalog;
  std::map<std::string, ParticleDefinition> Definitions;
  std::map<std::string, Bytes> Textures;  // Inflated original GXT, before GPU upload.
  static PreparedParticleEffects Prepare(const EffectResourceAssets& assets);
};
}  // namespace Impacto::Saekano
