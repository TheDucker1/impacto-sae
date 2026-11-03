#pragma once
#include "battleselection.h"
#include "messagedraw.h"

namespace Impacto::Saekano {
// Choice records have an independent lifetime from the two dialogue containers.
struct BattleChoiceText {
  VoiceTextReveal Reveal;
  PlainTextLayout Layout;
  void Prepare(std::u16string_view source,int32_t width,const TextFormatEnvironment& format,
      const OrdinaryTextEnvironment& shared,const GsfFont& font,const TextLayoutMetrics& metrics,
      uint32_t utilitySize,uint32_t nominalSize,uint64_t now);
};
std::array<int32_t,2> BattleChoiceTextOrigin(const BattleSelectionEnvironment& environment,
    size_t control,const PlainTextLayout& layout);
// Original8b058 body then footer calls. History/settings are explicit owning
// inputs; the renderer does not invent or update registered owner history.
std::vector<UiTextBlit> BuildBattleChoiceTextDraws(const BattleChoiceText& text,
    const BattleSelectionEnvironment& environment,const SharedSelectionEnvironment& selection,
    const TextFormatEnvironment& format,const GsfFont& font,uint32_t utilitySize,uint32_t nominalSize,
    size_t control,uint8_t opacity,bool historyColors,bool suppressed,bool selectedPreviously);
}
