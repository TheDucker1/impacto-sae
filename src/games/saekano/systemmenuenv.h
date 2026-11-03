#pragma once
#include "uisprite.h"
#include <map>

namespace Impacto::Saekano {
// Complete env_system_menu schema. Keys identify native destinations relative
// to 8026110c, independently of serialized offsets and collection lengths.
struct SystemMenuEnvironment {
  using Color = std::array<uint8_t, 4>;
  using Point = std::array<int32_t, 2>;
  using WordRecord = std::array<int32_t, 4>;
  struct IntegerList {
    int32_t Parameter = 0;
    std::vector<int32_t> Values;
  };
  std::array<uint8_t, 3> Flags{};
  std::map<uint16_t, uint32_t> Scalars;
  std::map<uint16_t, Color> Colors;
  std::map<uint16_t, UiSpriteDefinition> Sprites;
  std::map<uint16_t, Point> Points;
  std::map<uint16_t, std::vector<UiSpriteDefinition>> SpriteTables;
  std::map<uint16_t, std::vector<int32_t>> IntegerTables;
  std::map<uint16_t, std::vector<Color>> ColorTables;
  std::map<uint16_t, std::u16string> Strings;
  std::map<uint16_t, IntegerList> IntegerLists;
  std::map<uint16_t, std::vector<WordRecord>> WordTables;
  size_t Bytes = 0;
  static SystemMenuEnvironment Decode(std::span<const uint8_t> bytes);
};
}
