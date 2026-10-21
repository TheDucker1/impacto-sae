#include "resources.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <limits>

namespace Impacto::Saekano {
namespace {
Bytes RawEntry(const GpdaArchive& archive, size_t index) {
  if (archive.Entries().at(index).Type != 0)
    throw FormatError("expected uncompressed nested GPDA archive");
  return archive.Data(index);
}

Bytes CompressedEntry(const GpdaArchive& archive, size_t index, uint32_t expectedType) {
  if (archive.Entries().at(index).Type != expectedType)
    throw FormatError("unsupported GPDA resource entry type");
  return InflateGzip(archive.Data(index));
}

// Avoid retaining the complete 66 MB outer archive after taking its sections.
GpdaArchive ResourceSection(const GpdaArchive& root, size_t index) {
  return GpdaArchive(RawEntry(root, index));
}
}  // namespace

Bytes InflateGzip(std::span<const uint8_t> input, size_t outputLimit) {
  if (input.size() > std::numeric_limits<uInt>::max())
    throw FormatError("gzip input exceeds decoder range");
  z_stream stream{};
  if (inflateInit2(&stream, 15 + 16) != Z_OK)
    throw FormatError("cannot initialize gzip decoder");
  struct EndInflate {
    z_stream& Stream;
    ~EndInflate() { inflateEnd(&Stream); }
  } cleanup{stream};
  stream.next_in = const_cast<Bytef*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  Bytes result;
  std::array<uint8_t, 65536> block;
  for (;;) {
    stream.next_out = block.data();
    stream.avail_out = static_cast<uInt>(block.size());
    const auto before = stream.avail_in;
    const int status = inflate(&stream, Z_NO_FLUSH);
    const size_t produced = block.size() - stream.avail_out;
    if (produced > outputLimit - result.size())
      throw FormatError("gzip output exceeds resource budget");
    result.insert(result.end(), block.begin(), block.begin() + produced);
    if (status == Z_STREAM_END) {
      if (stream.avail_in != 0)
        throw FormatError("trailing data after gzip member");
      return result;
    }
    if (status != Z_OK || (produced == 0 && before == stream.avail_in))
      throw FormatError("invalid or truncated gzip resource");
  }
}

GameResources::GameResources(Bytes resourceDat)
    : GameResources(GpdaArchive(std::move(resourceDat))) {}

GameResources::GameResources(const GpdaArchive& root)
    : scripts_(ResourceSection(root, 4)), live2d_(ResourceSection(root, 0)) {}

Bytes GameResources::Load(int32_t scriptId) {
  if (scriptId < 0 || size_t(scriptId) >= ScriptCount())
    throw FormatError("script ID outside resource archive");
  // script.dat -> numbered script archive -> object archive -> gzip object.
  GpdaArchive script(RawEntry(scripts_, size_t(scriptId)));
  GpdaArchive object(RawEntry(script, 0));
  // Script objects are gzip bytes with type 0; model leaves use type 1.
  // The native script loader inflates explicitly rather than testing that word.
  return CompressedEntry(object, 0, 0);
}

Live2dModelAssets GameResources::LoadLive2d(size_t modelId) const {
  if (modelId >= Live2dCount())
    throw FormatError("Live2D model ID outside resource archive");
  GpdaArchive model(RawEntry(live2d_, modelId));
  Live2dModelAssets result;
  result.Name = live2d_.Entries()[modelId].Name;
  if (model.Entries().size() != result.Sections.size())
    throw FormatError("unsupported Live2D section layout");
  for (size_t index = 0; index < result.Sections.size(); ++index) {
    GpdaArchive section(RawEntry(model, index));
    auto& assets = result.Sections[index];
    assets.reserve(section.Entries().size());
    for (size_t asset = 0; asset < section.Entries().size(); ++asset)
      assets.push_back({section.Entries()[asset].Name,
                        CompressedEntry(section, asset, 1)});
  }
  return result;
}

}  // namespace Impacto::Saekano
