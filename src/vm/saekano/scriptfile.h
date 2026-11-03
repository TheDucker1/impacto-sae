#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "../../formaterror.h"

namespace Impacto::Saekano {

using Bytes = std::vector<uint8_t>;

using FormatError = Impacto::FormatError;

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
