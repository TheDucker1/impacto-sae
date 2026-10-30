#pragma once
#include "uisprite.h"
#include <map>

namespace Impacto::Saekano {
// Complete env_scene_title schema. Keys are offsets from native global60200,
// retained as research anchors until each presentation field has a proven name.
// They are configuration identities, never live addresses or VM cursor inputs.
struct TitleEnvironment {
  std::array<uint8_t,3> Flags{};
  std::map<uint16_t,uint32_t> Scalars;
  std::map<uint16_t,std::array<uint8_t,4>> Colors;
  std::map<uint16_t,UiSpriteDefinition> Sprites;
  std::map<uint16_t,std::array<int32_t,2>> Points;
  std::map<uint16_t,std::array<int32_t,4>> Rectangles;
  std::map<uint16_t,std::vector<int32_t>> IntegerTables;
  std::map<uint16_t,std::vector<std::array<uint8_t,4>>> ColorTables;
  std::map<uint16_t,std::vector<UiSpriteDefinition>> SpriteTables;
  std::map<uint16_t,std::vector<std::array<int32_t,2>>> PointTables;
  std::map<uint16_t,std::vector<std::array<int32_t,4>>> RectangleTables;
  std::vector<std::u16string> MenuLabels;
  static TitleEnvironment Decode(std::span<const uint8_t> bytes);
};
}
