#pragma once

#include "formats.h"
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
