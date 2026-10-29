#pragma once
#include "gauge.h"
#include "uisprite.h"
#include <functional>

namespace Impacto::Saekano {
struct BattleVersusCommand {
  std::array<std::u16string,2> Text;
  static BattleVersusCommand Decode(std::span<const uint8_t> payload);
};
struct BattleVersusEnvironment {
  int32_t Display=0, Offscreen=0, SlideDuration=0, FirstDuration=0,
      SecondDuration=0, TextDuration=0, TextTravel=0, IconDuration=0, FadeDuration=0;
  static BattleVersusEnvironment Decode(std::span<const uint8_t> bytes);
};
struct BattleVersusState {
  bool Known=false,Started=false,Visible=false,Completed=false;
  int32_t Phase=0;
  std::optional<int32_t> PendingPhase;
  std::array<std::u16string,2> Text;
  // fade, side0 X, side1 X, C14b20,14b58,14b90,14bc8.
  std::array<ScriptLinearTween,7> Tweens;
  std::array<bool,7> Enabled{};
  std::array<bool,2> Actors{};
};
struct SemanticState;
void StartBattleVersus(SemanticState& state,const BattleVersusEnvironment& environment,
                       Live2dDriver* models,uint64_t now);
bool UpdateBattleVersus(SemanticState& state,Live2dDriver* models,bool accept,uint64_t now);
struct BattleVersusVisualEnvironment {
  std::array<UiSpriteDefinition,4> Sprites;
  float NameScale=0,TextScale=0;
  std::vector<std::array<int32_t,2>> NamePositions,NameSizes,TextPositions;
  std::vector<int32_t> NameOffsets;
  std::vector<std::u16string> CharacterNames;
  int32_t TextPadding=0;
  std::vector<std::array<uint8_t,4>> Colors;
  static BattleVersusVisualEnvironment Decode(std::span<const uint8_t> bytes);
};
struct BattleVersusDraw {
  bool IsText=false;
  UiSpriteBlit Sprite;
  UiTextBlit Text;
};
std::vector<BattleVersusDraw> BuildBattleVersusDraws(const BattleVersusVisualEnvironment& environment,
    const BattleVersusState& state,std::array<int32_t,2> sides,uint32_t utilitySize,
    const std::function<int32_t(std::u16string_view,int32_t)>& measure);
}
