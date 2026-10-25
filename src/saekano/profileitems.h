#pragma once

#include "formats.h"
#include "uisprite.h"

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
struct ProfileDescriptionFormat {
  std::u16string Separator;
  size_t PrefixBytes = 0;
  // Proven prefix through global8025ca10, retaining only the needed separator.
  static ProfileDescriptionFormat Decode(std::span<const uint8_t> globalEnv);
};
std::array<std::u16string, 3> FormatProfileItemDescription(
    std::u16string_view text, std::u16string_view separator, uint32_t columns);
struct ProfileEnvironment;
struct ProfileCharacter;
struct ProfileItemSlotDraw {
  int32_t OwnerRow = 0, Category = 0;
  UiSpriteBlit Icon;
  UiTextBlit Label;
};
struct ProfileItemContents {
  UiSpriteBlit Selector;
  std::array<ProfileItemSlotDraw, 8> Slots;
};
// Native50472..506b6. Rows are the current owner's numeric row slots, not
// looked up by authored ID. Caller supplies current content tween samples;
// widget FrameOffsets have different completion-frame lifetime.
ProfileItemContents BuildProfileItemContents(
    const ProfileEnvironment& environment, int32_t phase,
    const ProfileCharacter& character, uint32_t selector,
    std::span<const ProfileItemDefinition> ownerRows, int32_t baseFontSize,
    std::span<const std::array<int32_t, 2>> contentOffsets = {});
struct ProfileDescriptionDraws {
  int32_t OwnerRow = 0;
  std::array<UiTextBlit, 3> Lines;
};
ProfileDescriptionDraws BuildProfileDescription(
    const ProfileEnvironment& environment, int32_t phase,
    const ProfileCharacter& character, uint32_t selector,
    std::span<const ProfileItemDefinition> ownerRows, int32_t baseFontSize,
    std::u16string_view separator);
}  // namespace Impacto::Saekano
