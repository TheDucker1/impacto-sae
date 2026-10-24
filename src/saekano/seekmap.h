#pragma once

#include "formats.h"

#include <string>
#include <string_view>

namespace Impacto::Saekano {
// Native800ac194: imported lowercase table, last extension removed, MSB CRC
// with seedFFFFFFFF and no final xor. Non-ASCII locale tables are not assumed.
uint32_t ResourceNameHashAscii(std::string_view name);
struct ScriptSeekRecord {
  std::string Name;
  uint32_t Hash = 0, Offset = 0, Size = 0, StoredHash = 0, Flags = 0;
};
class ScriptNameIndex {
 public:
  explicit ScriptNameIndex(std::span<const uint8_t> seekmapText);
  static ScriptNameIndex FromFirstArchive(Bytes firstDat);
  int32_t Resolve(std::string_view name) const;
  const std::vector<ScriptSeekRecord>& Records() const { return records_; }

 private:
  std::vector<ScriptSeekRecord> records_;
};
}  // namespace Impacto::Saekano
