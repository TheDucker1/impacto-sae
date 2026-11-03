#pragma once
#include "uisprite.h"
#include "slotbrowser.h"
#include <map>

namespace Impacto::Saekano {
// Complete env_saveload schema. Identities are relative to native8025ed88,
// independent of variable-length tables/strings in the input file.
struct SlotEnvironment {
  using Color=std::array<uint8_t,4>;
  using Point=std::array<int32_t,2>;
  using Rectangle=std::array<int32_t,4>;
  std::array<uint8_t,3> Flags{};
  std::map<uint16_t,uint32_t> Scalars; // Float fields retain exact source bits.
  std::map<uint16_t,Color> Colors;
  std::map<uint16_t,Point> Points;
  std::map<uint16_t,Rectangle> Rectangles;
  std::map<uint16_t,UiSpriteDefinition> Sprites;
  std::map<uint16_t,std::u16string> Strings;
  std::map<uint16_t,std::vector<Color>> ColorTables;
  std::map<uint16_t,std::vector<std::u16string>> StringTables;
  static SlotEnvironment Decode(std::span<const uint8_t> bytes);
  SlotBrowserSettings BrowserSettings()const;
};
}
