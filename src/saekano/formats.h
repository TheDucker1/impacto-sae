#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Impacto::Saekano {

using Bytes = std::vector<uint8_t>;

class FormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct GpdaEntry {
  uint32_t Offset;
  uint32_t Type;
  uint32_t LogicalSize;
  uint32_t NameOffset;
  uint32_t PhysicalSize;
  Bytes Name;  // Counted bytes: preserve spaces, embedded NUL and encoding.
};

// Own the input so nested archive views cannot outlive temporary file buffers.
// Native memory-open mutates type-2 signatures. Here Data() returns a copy, so
// raw input remains intact and the caller explicitly selects that conversion.
class GpdaArchive {
 public:
  explicit GpdaArchive(Bytes bytes);
  const std::vector<GpdaEntry>& Entries() const { return entries_; }
  uint32_t ArchiveEnd() const { return end_; }
  uint32_t ReservedWord() const { return reserved_; }
  Bytes Data(size_t index, bool physical = false,
             bool restoreGimSignature = false) const;

 private:
  Bytes bytes_;
  uint32_t end_ = 0;
  uint32_t reserved_ = 0;
  std::vector<GpdaEntry> entries_;
};

struct ScriptRecord {
  size_t Offset;
  uint32_t Size;  // Includes the six-byte record header.
  uint16_t Opcode;
  uint32_t PostCommandWord;  // Preserve without assuming source-line meaning.
};

class ScriptObject {
 public:
  explicit ScriptObject(Bytes bytes);
  const std::vector<ScriptRecord>& Records() const { return records_; }
  size_t RecordsOffset() const { return recordsOffset_; }
  size_t RecordsEnd() const { return recordsEnd_; }
  size_t FooterEnd() const { return footerEnd_; }
  std::span<const uint8_t> HeaderMetadata() const;
  std::span<const uint8_t> Tail() const;
  std::span<const uint8_t> Payload(size_t index) const;

 private:
  Bytes bytes_;
  std::vector<ScriptRecord> records_;
  size_t recordsOffset_ = 0;
  size_t recordsEnd_ = 0;
  size_t footerEnd_ = 0;
};

}  // namespace Impacto::Saekano
