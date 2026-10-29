#pragma once
#include "messagedraw.h"

namespace Impacto::Saekano {
struct BattleDialogueEnvironment {
  std::array<std::array<int32_t,2>,2> Positions{},LineOffsets{};
  UiSpriteDefinition Bubble;
  float Scale=0;
  int32_t TextWidth=0;
  static BattleDialogueEnvironment Decode(std::span<const uint8_t> bytes);
};
std::vector<UiSpriteBlit> BuildBattleDialogueBubble(const BattleDialogueEnvironment& environment,
    size_t side,uint32_t type,uint32_t openness,uint8_t opacity,std::array<int32_t,2> quake);
std::array<int32_t,2> BattleDialogueTextOrigin(const BattleDialogueEnvironment& environment,
    size_t side,uint16_t lineCount,uint16_t renderedWidth);
}
