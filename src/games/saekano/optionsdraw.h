#pragma once
#include "optionsowner.h"
namespace Impacto::Saekano {
enum class OptionsDrawKind {Sprite,Widget,Decoration,Page,Question,Save,Overlay};
struct OptionsDrawCommand {
  OptionsDrawKind Kind=OptionsDrawKind::Sprite;
  // Widget0..3 are tabs;4 is Revert. Page0..2 dispatches the actual page draw.
  uint32_t Index=0;
  UiSpriteBlit Sprite;
  std::array<int32_t,4> Rectangle{};
  std::array<uint8_t,4> Color{};
};
struct OptionsDrawInputs {
  bool AlternateHeader=false,QuestionPresent=false;
  int32_t SelectedTab=0,TabCount=3;
  std::array<bool,2> ArrowActive{};
  std::array<float,2> ArrowOffset{};
  int32_t FadeAlpha=255;
  // Actual globalca68, supplied by the shared decoration owner.
  std::array<int32_t,2> DecorationPosition{};
};
OptionsDrawInputs OptionsDrawFromState(const OptionsOwnerState& state,bool alternateHeader,
    bool questionPresent,std::array<int32_t,2> decorationPosition);
// Common native59214..599d0. Nested widget/page/decoration/modal draws remain
// explicit ordered commands, to be expanded by their existing/concrete owners.
std::vector<OptionsDrawCommand> BuildOptionsDraw(const OptionsEnvironment& environment,
                                               const OptionsDrawInputs& input);
}
