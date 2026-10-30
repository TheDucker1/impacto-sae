#pragma once
#include "gauge.h"
#include "uisprite.h"

namespace Impacto::Saekano {
struct SemanticState;
struct BattleResultCommand {
  std::array<int32_t,3> Destinations{};
  static BattleResultCommand Decode(std::span<const uint8_t> bytes);
};
struct BattleResultEnvironment {
  int32_t ItemAmplitude=0,ItemDuration=0,RevealDuration=0,FadeDuration=0;
  int32_t EqualPose=0,AbovePose=0,BelowPose=0;
  static BattleResultEnvironment Decode(std::span<const uint8_t> bytes);
};
enum class BattleResultAction {
  StopDialogueVoices,BeginResultMusic,SetPose,Sound,PlayVoice,Trophy,StopVoice,StopResultMusic,StopPrimaryVoices,Complete
};
struct BattleResultRequest {
  BattleResultAction Kind;
  int32_t Id=0,Parameter=0;
};
struct BattleResultState {
  bool Known=false,Started=false,Visible=false,Completed=false,RevealEnabled=false,FadeEnabled=false;
  int32_t Phase=0;
  std::optional<int32_t> PendingPhase;
  size_t Outcome=0,Item=0;
  BattleResultCommand Command;
  ScriptLinearTween Reveal,Fade;
  std::array<ScriptReversingTween,4> Items;
  std::array<ScriptLinearTween,4> Travel,Alpha;
  std::array<bool,4> TravelEnabled{},AlphaEnabled{};
  std::vector<BattleResultRequest> Requests;
};
struct BattleResultVisualEnvironment {
  // Equal/upper background and frame, then lower background and frame.
  std::array<UiSpriteDefinition,4> Sprites;
  std::array<uint8_t,4> GlyphColor{},UpperFrameColor{},LowerFrameColor{};
  std::vector<std::array<int32_t,4>> EqualRectangles,UpperRectangles,LowerRectangles;
  std::vector<std::array<int32_t,2>> EqualPositions,UpperPositions,LowerPositions,UpperBases,LowerBases;
  float UpperGlyphScale=0;
  static BattleResultVisualEnvironment Decode(std::span<const uint8_t> bytes);
};
// Native ScriptBattleMode_Draw7b3f2: versus pass has priority over result.
std::vector<UiSpriteBlit> BuildBattleResultDraws(const BattleResultVisualEnvironment& environment,
    const BattleResultState& result,bool versusVisible=false);
void StartBattleResult(SemanticState& owner,const BattleResultCommand& command,
    const BattleResultEnvironment& environment,uint64_t now);
// Normal and skip share this controller. The owning frontend MUST consume the
// ordered physical audio/UI/model/policy requests. This component alone is not
// a complete result backend; the production VM guard remains until it is bound.
bool UpdateBattleResult(SemanticState& owner,const BattleResultEnvironment& environment,
    bool accept,bool auxiliaryPressed,uint64_t now);
}
