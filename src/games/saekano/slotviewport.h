#pragma once
#include "slotenv.h"
namespace Impacto::Saekano {
struct SlotCardRange {
  int32_t FirstVisible=0,Selected=0;
  std::array<int32_t,2> Offsets{};
  int32_t Begin=0,End=0; // Exclusive end; native card callback clips to0..59.
};
enum class SlotViewportKind {Render,Cards,StaticContent};
struct SlotViewportCommand {
  SlotViewportKind Kind=SlotViewportKind::Render;
  UiRenderCommand Render;
  SlotCardRange Cards;
};
struct SlotViewportInputs {
  bool Visible=true,ExternalMask=false,Animated=true,Scrolling=false;
  OptionsRowNavigation Rows;
  SlotEnvironment::Rectangle Rectangle{};
  std::array<int32_t,2> Canvas{960,544};
  int32_t ScrollOffset=0,CursorOffset=0,Pitch=0;
};
SlotViewportInputs SlotViewportFromState(const SlotEnvironment& environment,const SlotBrowserState& browser,
    bool visible=true,bool externalMask=false);
// Native209a4: stencil lifecycle plus exact callback ranges, including extra
// rows and previous selection while scrolling. Rasterization is caller-owned.
std::vector<SlotViewportCommand> BuildSlotViewport(const SlotViewportInputs& input);
}
