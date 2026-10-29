#pragma once

#include "formats.h"
#include "random.h"
#include <array>
#include <string>
#include <map>
#include <optional>

namespace Impacto::Saekano {
struct ParticleGroupClock {
  static constexpr uint64_t TickMilliseconds = 16;
  std::array<bool, 8> Paused{};
  // Intended pause semantics: no owner update while paused; resume with one
  // normal tick. Do not reproduce native held-time subtraction/underflow.
  std::optional<uint64_t> Delta(size_t group) const;
};
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
std::array<float, 3> TransformParticleEmitterPosition(const std::array<float, 3>& position,
    const std::array<float, 3>& pivot, const std::array<float, 3>& angles);
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
struct ParticleDrawPacket {
  std::array<uint32_t, 2> Size{};
  std::array<int32_t, 4> Source{};  // Cell X/Y/width/height in texture pixels.
  std::array<float, 3> Position{}, Angles{};
  std::array<uint8_t, 4> Bgra{};
  float Intensity = 0;
  int32_t RenderMode = 0;  // Definition48 selects shader mode, not blend factors.
};
// Native98c8 (immediate) /9b22 (batch) fields at submission boundary. Vertex
// projection/UV construction and batch sorting are separate consumers.
std::optional<ParticleDrawPacket> BuildParticleDrawPacket(const ParticleRuntimeState& state,
    const ParticleDefinition& definition, uint32_t textureWidth, uint32_t textureHeight,
    float groupOpacity, bool textureReady, bool batched);
struct ParticleBatchCandidate {
  const ParticleRuntimeState* Particle = nullptr;
  std::optional<size_t> TextureIndex;  // Index in the owner's native texture order.
};
struct ParticleVertex {
  std::array<float, 3> LocalPosition{};
  std::array<uint8_t, 4> Rgba{};
  std::array<float, 2> Uv{};
  std::array<float, 3> Position{}, Angles{};
};
static_assert(sizeof(ParticleVertex) == 48);
struct ParticleProjectionUniforms {
  std::array<float, 16> View{}, Projection{}, World{};
  float Aspect = 0;
  // Native GXP default uniform slots0,16,32,48; final padding is zero.
  std::array<float, 52> Pack() const;
};
struct ParticleBlendConfiguration {
  bool Enabled = false;
  // Native GXM factor indices:0 zero,1 one,2 source color,4 source alpha,
  //5 inverse source alpha. Color/alpha channels remain independent.
  uint8_t ColorSource = 0, ColorDestination = 0, AlphaSource = 0, AlphaDestination = 0;
};
// Fragment-program table index (0..5), not the public native blend-mode value.
ParticleBlendConfiguration ParticleBlend(size_t index);
// State inherited by particle and ordinary textured draws. These are separate
// selectors from definition48's shader mode; setters preserve native retention.
struct ParticleFragmentState {
  uint32_t BlendIndex = 0, AlphaFamily = 0;
  float AlphaThreshold = 0;
  void SetBlendMode(int32_t mode);
  void SetAlphaTest(bool enabled, int32_t comparison, uint8_t reference);
};
std::array<float,16> ApproachParticleWorld();
// Native c1818/c0ed0: centered local corners and half-texel UVs. Position and
// rotation remain separate attributes; the vertex shader applies projection.
std::array<ParticleVertex, 4> BuildParticleQuad(const ParticleDrawPacket& packet,
    uint32_t textureWidth, uint32_t textureHeight);
std::array<ParticleVertex, 6> BuildParticleTriangles(const ParticleDrawPacket& packet,
    uint32_t textureWidth, uint32_t textureHeight);
struct ParticleTextureBatch {
  size_t TextureIndex = 0;
  bool Translucent = false;
  std::vector<size_t> Candidates;  // Indices in the input traversal order.
};
// Native d670/d5c2/d456: retain first300 collected entries, partition by the
// twice-truncated alpha, reproduce depth-sort ties, then emit per-texture lists.
std::vector<ParticleTextureBatch> BuildParticleBatches(
    std::span<const ParticleBatchCandidate> candidates, size_t textureCount, float groupOpacity);
struct ParticlePointEmitter {
  // Native ADD_POINT_POINT callback8000ef4c, command type5. Names are raw
  // narrow bytes. Consumer meanings/timing units of the numeric fields follow.
  std::string ParticleName, TextureName;
  std::array<int32_t, 3> Integers{};
  std::array<float, 9> Values{};
  static ParticlePointEmitter DecodeRow(std::span<const uint8_t> bytes);
  // Native creation type5: base + from + (to-from)*emitted/count, f32 stages.
  std::array<float, 3> Origin(int32_t emitted) const;
  std::array<float, 14> Creation(int32_t emitted,
      const std::array<float, 14>& owner, float headerScalar) const;
};
struct ParticleEffectHeader {
  std::string Name;
  std::array<int32_t, 2> Integers{};
  float Scalar = 0;
  static ParticleEffectHeader DecodeRow(std::span<const uint8_t> bytes);
};
struct ParticleEmitterState {
  int32_t Emitted = 0;
  std::vector<ParticleRuntimeState> Particles;  // Native list traversal/birth order.
  // Resources must already be resolved and texture-ready. Retirement does not
  // erase entries; owner initialization and counter reset are separate actions.
  void Emit(const ParticlePointEmitter& point, const ParticleDefinition& definition,
      const std::array<float, 14>& owner, const ParticleEffectHeader& header,
      const std::array<float, 2>& originOffset, GameRandom& random);
};
struct ParticleEffectProgram {
  ParticleEffectHeader Header;
  std::vector<ParticlePointEmitter> Points;
};
// Plan due emissions in native row order. The caller executes each event with
// Emit (which advances its row count); this planner does not mutate storage.
std::vector<size_t> DueParticleEmissions(const ParticleEffectProgram& program,
    std::span<const ParticleEmitterState> rows, int32_t counter,
    int32_t startOffset, bool initializing);
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
struct ParticleEffectState {
  std::vector<ParticleEmitterState> Rows;
  std::array<float, 14> Owner{};
  std::array<float, 2> OriginOffset{};
  int32_t Counter = 0, StartOffset = 0, Repeats = 0;
  bool Enabled = true, Running = true, Emitting = true;
  // Native494e updates all stored particles, including retired entries.
  void SetOriginOffset(const std::array<float, 2>& offset);
  void Start(const ParticleEffectProgram& program, const PreparedParticleEffects& assets, GameRandom& random);
  bool Update(const ParticleEffectProgram& program, const PreparedParticleEffects& assets,
              uint64_t elapsedDelta, GameRandom& random);
};
class GameResources;
struct ConfiguredParticleEffect {
  size_t ArchiveIndex = 0, ProgramIndex = 0;
  PreparedParticleEffects Assets;
  const ParticleEffectProgram& Program() const { return Assets.Catalog.Programs.at(ProgramIndex); }
  // Search original archive/CSV order by EFFECT header name. Only the matched
  // archive is fully prepared, so unrelated command families remain separate.
  static ConfiguredParticleEffect Load(const GameResources& resources,
      const ParticleEnvironment& environment, size_t effectId);
};
struct ParticleEffectRequest {
  int32_t StartOffset = 0;
  uint32_t Group = 0;
  std::array<float, 14> Owner{};  // Exact native request words2..15.
};
struct ParticleEffectHandle {
  std::optional<size_t> EffectId;
  ParticleEffectState State;
};
class ParticleManager {
 public:
  static constexpr size_t GroupCount = 8, HandlesPerGroup = 25;
  using GroupHandles = std::array<ParticleEffectHandle, HandlesPerGroup>;
  explicit ParticleManager(uint64_t nativeClock);
  // Cache once before requests. Existing IDs retain their original resources.
  void LoadEffect(const GameResources& resources, const ParticleEnvironment& environment, size_t effectId);
  void RegisterEffect(size_t effectId, ConfiguredParticleEffect effect);
  bool HasEffect(size_t effectId) const { return effects_.contains(effectId); }
  // Like native dcc6, true means a known resource, even if the pool was full.
  // Prewarm advances the entire manager in16ms steps, not only the new handle.
  bool Request(size_t effectId, const ParticleEffectRequest& request, int32_t prewarmMilliseconds = 0);
  void Update();  // Simulation only; GPU preparation remains a separate consumer.
  void SetPaused(size_t group, bool paused);
  void SetOriginOffset(size_t group, const std::array<float, 2>& offset);
  const GroupHandles& Handles(size_t group) const;
  const ConfiguredParticleEffect& Effect(size_t id) const { return effects_.at(id); }
  const GameRandom& Random() const { return random_.Generator; }

 private:
  std::map<size_t, ConfiguredParticleEffect> effects_;
  std::array<GroupHandles, GroupCount> groups_;
  std::array<std::array<float, 2>, GroupCount> origins_{};
  ParticleRandomState random_;
  ParticleGroupClock clock_;
};
}  // namespace Impacto::Saekano
