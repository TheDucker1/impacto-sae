#pragma once
#include "titlecontroller.h"

namespace Impacto::Saekano {
struct TitleDrawInputs {
  int32_t Phase=0,Selected=0,Character=0;
  bool ResourceDrawBlocked=false;
  int32_t Reveal=0,LogoMove=0,LogoDetail=0,MenuMove=0,MenuDetail=0;
  std::array<int32_t,4> Buttons{};
  int32_t BlinkAlpha=0;
  std::array<int32_t,2> AmbientDegrees{};
  uint32_t FontSize=27;
};
enum class TitleDrawKind { Clear,Blend,TransformReset,Sprite,Text,Widget,Effect };
struct TitleDrawCommand {
  TitleDrawKind Kind=TitleDrawKind::Sprite;
  UiSpriteBlit Sprite{};
  UiTextBlit Text{};
  uint32_t Index=0;
};
TitleDrawInputs TitleDrawFromState(const TitleState& state,int32_t blinkAlpha,
                                  std::array<int32_t,2> ambientDegrees,uint32_t fontSize);
// Main draw3612c only. Overlay35fd6, widget/effect internals and rasterization
// remain frontend consumers of these ordered commands.
std::vector<TitleDrawCommand> BuildTitleDraw(const TitleEnvironment& env,const TitleDrawInputs& inputs);
}
