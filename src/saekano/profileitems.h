#pragma once

#include "formats.h"

namespace Impacto::Saekano {
struct ProfileItemDefinition {
  int32_t Id = 0, Category = 0;
  std::u16string Name, Description;
};
// Accepted ON rows retain their serialized order; the authored ID is a field,
// not an array index. Unwritten native numeric slots stay absent.
std::vector<ProfileItemDefinition> DecodeProfileItemTable(
    std::span<const uint8_t> bytes);
struct ProfileItemCharacterTable {
  Bytes Name;
  std::vector<ProfileItemDefinition> Rows;
};
// first.dat section3 contains UTF16 item tables, not resource.dat scene CSVs.
std::vector<ProfileItemCharacterTable> LoadProfileItemCatalog(Bytes firstDat);
}  // namespace Impacto::Saekano
