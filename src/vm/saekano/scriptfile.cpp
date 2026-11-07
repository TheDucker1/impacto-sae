#include "scriptfile.h"

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
    const uint16_t opcode =
        uint16_t(bytes_[cursor + 4]) | (uint16_t(bytes_[cursor + 5]) << 8);
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
