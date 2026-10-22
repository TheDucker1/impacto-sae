#include "image.h"

#include "resources.h"
#include "../texture/palettecolor.h"

#include <algorithm>

namespace Impacto::Saekano {
namespace {
uint32_t U32(std::span<const uint8_t> bytes, size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    throw FormatError("truncated scene GXT header");
  return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
         uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
}
}  // namespace
ImagePixels DecodeSceneImage(std::span<const uint8_t> bytes) {
  if (bytes.size() < 64 || U32(bytes, 0) != 0x00545847 ||
      U32(bytes, 4) != 0x10000003 || U32(bytes, 8) != 1 ||
      U32(bytes, 20) != 0 || U32(bytes, 24) != 1 || U32(bytes, 40) != 0 ||
      U32(bytes, 48) != 0x60000000 ||
      (U32(bytes, 52) != 0x95001000 && U32(bytes, 52) != 0x95005000) ||
      U32(bytes, 60) != 1)
    throw FormatError("unsupported scene GXT layout");
  const uint32_t dataBegin = U32(bytes, 12), dataSize = U32(bytes, 16);
  const uint32_t pixelsBegin = U32(bytes, 32), pixelsSize = U32(bytes, 36);
  const auto dimensions = U32(bytes, 56);
  ImagePixels result;
  result.Width = dimensions & 0xFFFF;
  result.Height = dimensions >> 16;
  const uint64_t count = uint64_t(result.Width) * result.Height;
  if (result.Width == 0 || result.Height == 0 || count > 64 * 1024 * 1024 ||
      dataBegin < 64 || pixelsBegin != dataBegin || count != pixelsSize ||
      uint64_t(pixelsSize) + 1024 != dataSize ||
      uint64_t(dataBegin) + dataSize != bytes.size())
    throw FormatError("invalid scene GXT data extent");
  const auto palette = bytes.subspan(size_t(pixelsBegin) + pixelsSize, 1024);
  const bool hasAlpha = U32(bytes, 52) == 0x95001000;
  result.Rgba.resize(size_t(count) * 4);
  for (size_t i = 0; i < count; ++i) {
    const auto color =
        TexLoad::PaletteColor(palette, bytes[pixelsBegin + i], hasAlpha);
    std::copy(color.begin(), color.end(), result.Rgba.begin() + i * 4);
  }
  return result;
}
void ArchiveSceneImages::Prepare(ImageSlot slot, ImageKey key) {
  auto image = cache_[key].lock();
  if (!image) {
    auto asset = resources_.LoadImage(key.Section, key.Id);
    image = std::make_shared<ImagePixels>(DecodeSceneImage(asset.Data));
    cache_[key] = image;
  }
  slots_.at(size_t(slot)) = std::move(image);
  keys_.at(size_t(slot)) = key;
}
void ArchiveSceneImages::Release(ImageSlot slot) {
  slots_.at(size_t(slot)).reset();
  keys_.at(size_t(slot)).reset();
}
const ImagePixels* ArchiveSceneImages::Pixels(ImageSlot slot) const {
  return slots_.at(size_t(slot)).get();
}
std::optional<ImageKey> ArchiveSceneImages::Key(ImageSlot slot) const {
  return keys_.at(size_t(slot));
}
}  // namespace Impacto::Saekano
