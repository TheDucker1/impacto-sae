#include "imagesave.h"

#include <bit>

namespace Impacto::Saekano {
namespace {
class Reader {
 public:
  explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  uint32_t Read(size_t count) {
    if (count > bytes_.size() - cursor_)
      throw FormatError("truncated native image save prefix");
    uint32_t value = 0;
    for (size_t i = 0; i < count; ++i)
      value |= uint32_t(bytes_[cursor_++]) << (i * 8);
    return value;
  }
  int16_t Short() { return std::bit_cast<int16_t>(uint16_t(Read(2))); }
  size_t Position() const { return cursor_; }

 private:
  std::span<const uint8_t> bytes_;
  size_t cursor_ = 0;
};
}  // namespace
NativeImageSavePrefix DecodeNativeImageSavePrefix(
    std::span<const uint8_t> bytes) {
  Reader reader(bytes);
  NativeImageSavePrefix result;
  for (auto& rect : result.FillRects) {
    rect.PackedColor = reader.Read(4);
    rect.Opacity = std::bit_cast<int32_t>(reader.Read(4));
    rect.Bucket = reader.Read(4);
  }
  for (auto& group : result.FloatSlots)
    for (auto& slot : group) {
      slot.Flag = uint8_t(reader.Read(1));
      slot.Texture = reader.Short();
      for (auto& value : slot.Parameters) value = uint8_t(reader.Read(1));
      for (auto& value : slot.Words) value = reader.Read(4);
    }
  for (auto& slot : result.IntegerSlots) {
    slot.Flag = uint8_t(reader.Read(1));
    for (auto& value : slot.GeometryAndTexture) value = reader.Short();
    slot.Parameter4 = uint8_t(reader.Read(1));
    slot.Parameter5 = reader.Short();
    slot.Parameter6 = uint8_t(reader.Read(1));
    for (auto& value : slot.Words) value = reader.Read(4);
  }
  for (auto& references : result.TextureReferences) {
    const auto count = reader.Read(4);
    if (count > 1000000 || count > (bytes.size() - reader.Position()) / 4)
      throw FormatError("invalid native image texture reference count");
    references.reserve(count);
    for (uint32_t i = 0; i < count; ++i) references.push_back(reader.Read(4));
  }
  result.EndOffset = reader.Position();
  return result;
}
}  // namespace Impacto::Saekano
