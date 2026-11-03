#pragma once
#include "slotenv.h"
#include "slotcontroller.h"
namespace Impacto::Saekano {
enum class SlotDrawKind {Sprite,Widget,Browser,Decoration,Question,Save,Rectangle};
struct SlotDrawCommand {
  SlotDrawKind Kind=SlotDrawKind::Sprite;
  SlotBrowserWidget Widget=SlotBrowserWidget::Page;
  uint32_t Index=0;
  UiSpriteBlit Sprite;
  std::array<int32_t,4> Rectangle{};
  std::array<uint8_t,4> Color{};
};
struct SlotDrawInputs {
  int32_t Mode=1,Selected=0,FadeAlpha=0;
  bool Context=false,QuestionPresent=false;
  std::array<int32_t,2> DecorationPosition{};
};
SlotDrawInputs SlotDrawFromState(const SlotControllerState& controller,
    const SlotBrowserState& browser,bool context,bool question,std::array<int32_t,2> decoration);
// Native5ceb2..5db9c. Nested slot cards/widgets/help/modal owners remain explicit
// commands, preserving native order instead of substituting their content.
std::vector<SlotDrawCommand> BuildSlotDraw(const SlotEnvironment& environment,const SlotDrawInputs& input);
}
