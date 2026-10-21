#pragma once

#include "vm.h"

#include <array>

namespace Impacto::Saekano {

// One gzip member, with CRC/ISIZE validation and an explicit output budget.
// Archive padding is not compressed input: pass the entry's logical extent.
Bytes InflateGzip(std::span<const uint8_t> input,
                  size_t outputLimit = 256 * 1024 * 1024);

struct ResourceAsset {
  Bytes Name;  // Native counted bytes, including trailing spaces.
  Bytes Data;
};

// These are native archive section indexes, not alphabetical filename order.
enum class Live2dSection : size_t {
  Moc, Textures, Pose, Expressions, LoopMotions, OneShotMotions,
  AngerMotions, SweatMotions, TearMotions, Count
};

struct Live2dModelAssets {
  Bytes Name;
  std::array<std::vector<ResourceAsset>, size_t(Live2dSection::Count)> Sections;
  const std::vector<ResourceAsset>& Get(Live2dSection section) const {
    return Sections.at(size_t(section));
  }
};

// Opens resource.dat directly; callers need no extracted script or asset tree.
// Assets remain owned bytes until their renderer/audio consumer accepts them.
// Decoding MOC geometry and GXT pixels is a separate consumer responsibility.
class GameResources final : public ScriptSource {
 public:
  explicit GameResources(Bytes resourceDat);
  Bytes Load(int32_t scriptId) override;
  Live2dModelAssets LoadLive2d(size_t modelId) const;
  size_t ScriptCount() const { return scripts_.Entries().size(); }
  size_t Live2dCount() const { return live2d_.Entries().size(); }
  const Bytes& ScriptName(size_t id) const { return scripts_.Entries().at(id).Name; }

 private:
  explicit GameResources(const GpdaArchive& root);
  GpdaArchive scripts_;
  GpdaArchive live2d_;
};

}  // namespace Impacto::Saekano
