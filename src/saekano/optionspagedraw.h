#pragma once
#include "optionsdraw.h"
#include "font.h"
namespace Impacto::Saekano {
enum class OptionsPageDrawKind {Sprite,Widget,Text};
struct OptionsPageDrawCommand {
  OptionsPageDrawKind Kind=OptionsPageDrawKind::Sprite;
  UiSpriteBlit Sprite;
  OptionsWidgetId Widget;
  UiTextBlit Text;
};
struct OptionsPageDrawInputs {
  uint32_t Page=0;
  OptionsValues Values{};
  std::array<int32_t,3> SelectedRows{};
  std::array<int32_t,3> TextX{};
  std::array<int32_t,5> AudioX{};
  int32_t Character=0,HighlightProgress=0;
  uint8_t HighlightAlpha=255;
};
OptionsPageDrawInputs OptionsPageDrawFromState(const OptionsOwnerState& owner,uint8_t highlightAlpha);
// Native58a9c(text),58424(audio),57b2e(voice). Widget group visuals remain
// explicit calls; numeric/toggle/icon/highlight/help-text submissions are real.
std::vector<OptionsPageDrawCommand> BuildOptionsPageDraw(const OptionsEnvironment& environment,
    const OptionsPageDrawInputs& input,const GsfFont& font,uint32_t utilitySize,uint32_t nominalSize);
}
