#pragma once
#include "uisprite.h"
#include <map>
namespace Impacto::Saekano {
// env_debug prefix through native8025c267, read by Options final audio apply.
struct OptionsDebugSettings {
  std::array<uint8_t,3> Flags{};
  uint8_t Silent=0;
  uint8_t UnlockMovies=0,UnlockCg=0,UnlockMusic=0,UnlockBattle=0;
  size_t PrefixBytes=0;
  bool ForceSilent()const{return Silent!=0;}
  static OptionsDebugSettings Decode(std::span<const uint8_t> bytes);
};
// Complete env_option schema. Keys are identities relative to native8025e0d4,
// not file positions. Counted table sizes determine subsequent file positions.
struct OptionsEnvironment {
  using Point=std::array<int32_t,2>;
  using Rectangle=std::array<int32_t,4>;
  std::array<uint8_t,3> Flags{};
  std::map<uint16_t,uint32_t> Scalars;
  std::map<uint16_t,std::array<uint8_t,4>> Colors;
  std::map<uint16_t,UiSpriteDefinition> Sprites;
  std::map<uint16_t,Point> Points;
  std::map<uint16_t,Rectangle> Rectangles;
  std::map<uint16_t,std::vector<int32_t>> IntegerTables;
  std::map<uint16_t,std::vector<Point>> PointTables;
  std::map<uint16_t,std::vector<UiSpriteDefinition>> SpriteTables;
  std::map<uint16_t,std::vector<std::vector<Rectangle>>> RectangleRows;
  std::map<uint16_t,std::vector<std::vector<Point>>> PointRows;
  std::map<uint16_t,std::vector<std::vector<std::u16string>>> StringRows;
  std::map<uint16_t,std::vector<std::vector<std::vector<std::u16string>>>> StringGrids;
  std::u16string SaveQuestion;
  static OptionsEnvironment Decode(std::span<const uint8_t> bytes);
};
}
