#include "formats.h"

#include <algorithm>
#include <array>
#include <utility>

namespace Impacto::Saekano {
namespace {

void RequireRange(size_t offset, size_t size, size_t limit) {
  // Subtraction avoids wraparound on hostile 32-bit on-disk sizes.
  if (offset > limit || size > limit - offset)
    throw FormatError("data range outside declared container");
}

uint32_t U32(std::span<const uint8_t> bytes, size_t offset) {
  RequireRange(offset, 4, bytes.size());
  return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
         (uint32_t(bytes[offset + 2]) << 16) |
         (uint32_t(bytes[offset + 3]) << 24);
}

}  // namespace

GpdaArchive::GpdaArchive(Bytes bytes) : bytes_(std::move(bytes)) {
  RequireRange(0, 16, bytes_.size());
  constexpr std::array<uint8_t, 4> Magic{'G', 'P', 'D', 'A'};
  if (!std::equal(Magic.begin(), Magic.end(), bytes_.begin()))
    throw FormatError("invalid GPDA signature");
  end_ = U32(bytes_, 4);
  reserved_ = U32(bytes_, 8);
  const uint32_t count = U32(bytes_, 12);
  RequireRange(0, end_, bytes_.size());
  if (end_ < 16 || count > (end_ - 16) / 16)
    throw FormatError("truncated GPDA entry table");
  const size_t tableEnd = 16 + size_t(count) * 16;
  entries_.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t base = 16 + 16 * i;
    GpdaEntry entry{U32(bytes_, base), U32(bytes_, base + 4),
                    U32(bytes_, base + 8), U32(bytes_, base + 12), 0, {}};
    if (entry.Offset < tableEnd)
      throw FormatError("GPDA payload overlaps entry table");
    RequireRange(entry.Offset, entry.LogicalSize, end_);
    if (entry.NameOffset != 0) {
      RequireRange(entry.NameOffset, 4, end_);
      const uint32_t length = U32(bytes_, entry.NameOffset);
      const size_t nameStart = size_t(entry.NameOffset) + 4;
      RequireRange(nameStart, length, end_);
      entry.Name.assign(bytes_.begin() + nameStart,
                        bytes_.begin() + nameStart + length);
    }
    entries_.push_back(std::move(entry));
  }
  for (size_t i = 0; i < entries_.size(); ++i) {
    auto& entry = entries_[i];
    const uint32_t next = i + 1 < entries_.size() ? entries_[i + 1].Offset : end_;
    if (next < entry.Offset || entry.LogicalSize > next - entry.Offset)
      throw FormatError("overlapping or unordered GPDA payloads");
    entry.PhysicalSize = next - entry.Offset;
  }
}

Bytes GpdaArchive::Data(size_t index, bool physical,
                        bool restoreGimSignature) const {
  const auto& entry = entries_.at(index);
  const size_t size = physical ? entry.PhysicalSize : entry.LogicalSize;
  Bytes result(bytes_.begin() + entry.Offset,
               bytes_.begin() + entry.Offset + size);
  if (restoreGimSignature && entry.Type == 2) {
    constexpr std::array<uint8_t, 16> Signature{
        'M', 'I', 'G', '.', '0', '0', '.', '1', 'P', 'S', 'P', 0, 0, 0, 0, 0};
    if (result.size() < Signature.size())
      throw FormatError("type-2 GPDA payload too short for GIM signature");
    std::copy(Signature.begin(), Signature.end(), result.begin());
  }
  return result;
}

ScriptObject::ScriptObject(Bytes bytes) : bytes_(std::move(bytes)) {
  const uint32_t count = U32(bytes_, 0);
  recordsOffset_ = U32(bytes_, 4);
  if (recordsOffset_ < 8 || recordsOffset_ > bytes_.size())
    throw FormatError("invalid script first-record displacement");
  // At least six record bytes and four post-command bytes per command.
  if (count > (bytes_.size() - recordsOffset_) / 10)
    throw FormatError("script command count exceeds available bytes");
  records_.reserve(count);
  size_t cursor = recordsOffset_;
  for (size_t i = 0; i < count; ++i) {
    RequireRange(cursor, 6, bytes_.size());
    const uint32_t size = U32(bytes_, cursor);
    if (size < 6) throw FormatError("script record smaller than header");
    RequireRange(cursor, size, bytes_.size());
    const uint16_t opcode = uint16_t(bytes_[cursor + 4]) |
                            (uint16_t(bytes_[cursor + 5]) << 8);
    records_.push_back({cursor, size, opcode, 0});
    cursor += size;  // Size includes itself. No rounding to alignment.
  }
  recordsEnd_ = cursor;
  if (count > (bytes_.size() - cursor) / 4)
    throw FormatError("truncated script post-command table");
  for (auto& record : records_) {
    record.PostCommandWord = U32(bytes_, cursor);
    cursor += 4;
  }
  footerEnd_ = cursor;
}

std::span<const uint8_t> ScriptObject::HeaderMetadata() const {
  return std::span<const uint8_t>(bytes_).subspan(8, recordsOffset_ - 8);
}
std::span<const uint8_t> ScriptObject::Tail() const {
  return std::span<const uint8_t>(bytes_).subspan(footerEnd_);
}
std::span<const uint8_t> ScriptObject::Payload(size_t index) const {
  const auto& record = records_.at(index);
  return std::span<const uint8_t>(bytes_).subspan(record.Offset + 6,
                                                record.Size - 6);
}

}  // namespace Impacto::Saekano
