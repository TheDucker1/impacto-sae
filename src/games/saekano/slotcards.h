#pragma once
#include "slotviewport.h"
#include "slotdata.h"
#include "font.h"
namespace Impacto::Saekano {
enum class SlotCardDrawKind {Sprite,Text,NewIcon};
struct SlotCardDrawCommand {
  SlotCardDrawKind Kind=SlotCardDrawKind::Sprite;
  UiSpriteBlit Sprite;
  UiTextBlit Text;
  std::array<int32_t,2> Position{}; // Original shared animated new-marker owner.
};
// Native5ace8. Thumbnail surfaces are caller-owned; animated marker rasterization
// is a call to the real shared owner, not a frozen substitute sprite.
std::vector<SlotCardDrawCommand> BuildSlotCards(const SlotEnvironment& environment,
    std::span<const NativeSlotRecord> records,int32_t mode,const SlotCardRange& range,
    uint8_t highlightAlpha,const GsfFont& font,uint32_t utilitySize,uint32_t nominalSize);
}
