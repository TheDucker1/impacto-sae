#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../formaterror.h"

namespace Impacto::Io {

struct GpdaEntry {
  uint32_t Offset;
  uint32_t Type;
  uint32_t LogicalSize;
  uint32_t NameOffset;
  uint32_t PhysicalSize;
  std::vector<uint8_t> Name;  // Counted bytes retain spaces, NULs and encoding.
};

// Own the input so nested archive views cannot outlive temporary file buffers.
// Data returns a copy; restoring a type-2 GIM signature leaves the input
// intact.
class GpdaArchive {
 public:
  explicit GpdaArchive(std::vector<uint8_t> bytes);
  const std::vector<GpdaEntry>& Entries() const { return entries_; }
  uint32_t ArchiveEnd() const { return end_; }
  uint32_t ReservedWord() const { return reserved_; }
  std::vector<uint8_t> Data(size_t index, bool physical = false,
                            bool restoreGimSignature = false) const;

 private:
  std::vector<uint8_t> bytes_;
  uint32_t end_ = 0;
  uint32_t reserved_ = 0;
  std::vector<GpdaEntry> entries_;
};

}  // namespace Impacto::Io
