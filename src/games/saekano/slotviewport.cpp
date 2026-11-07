#include "slotviewport.h"
#include "commands.h"
#include <bit>
namespace Impacto::Saekano {
namespace {int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}}
SlotViewportInputs SlotViewportFromState(const SlotEnvironment& e,const SlotBrowserState& b,bool visible,bool mask){
  if(!b.Started)throw UnsupportedCommand("slot viewport has no browser owner");
  SlotViewportInputs in;in.Visible=visible;in.ExternalMask=mask;in.Animated=b.RenderEnabled;in.Scrolling=b.Scrolling;in.Rows=b.Rows;
  in.Rectangle=e.Rectangles.at(0x148);in.ScrollOffset=b.Scroll.Current;in.CursorOffset=b.Cursor.Current;in.Pitch=b.Settings.Pitch;return in;
}
std::vector<SlotViewportCommand> BuildSlotViewport(const SlotViewportInputs& in){
  std::vector<SlotViewportCommand> out;if(!in.Visible)return out;
  if(in.Canvas[0]<=0 || in.Canvas[1]<=0 || in.Rectangle[2]<0 || in.Rectangle[3]<0)throw FormatError("invalid slot viewport extent");
  const auto render=[&](UiRenderKind kind,int32_t mode=0,std::optional<bool> enabled=std::nullopt){SlotViewportCommand c;c.Render.Kind=kind;c.Render.Mode=mode;c.Render.TestEnabled=enabled;out.push_back(c);};
  const auto rectangle=[&](std::array<int32_t,4> r,std::array<uint8_t,4> color){render(UiRenderKind::Rectangle);out.back().Render.Rectangle=r;out.back().Render.Color=color;};
  if(!in.ExternalMask){
    render(UiRenderKind::ResetDepthStencilBackground);render(UiRenderKind::Stencil,1);render(UiRenderKind::DepthTest,0,true);render(UiRenderKind::AlphaTest,6,true);
    rectangle({0,0,in.Canvas[0],in.Canvas[1]},{0,0,0,1});render(UiRenderKind::Stencil,2);rectangle(in.Rectangle,{255,255,255,255});
    render(UiRenderKind::Stencil,3);render(UiRenderKind::AlphaTest,3,true);render(UiRenderKind::DepthTest,5,true);
  }
  SlotViewportCommand c;
  if(!in.Animated)c.Kind=SlotViewportKind::StaticContent;
  else{
    if(in.Pitch<=0 || in.Rows.Visible<=0)throw FormatError("invalid slot callback extent");
    c.Kind=SlotViewportKind::Cards;const auto first=Add(in.Rows.PreviousScroll,in.ScrollOffset/in.Pitch);
    c.Cards.Begin=Add(first,-1);c.Cards.End=Add(Add(first,in.Rows.Visible),1);
    if(in.Scrolling){c.Cards.FirstVisible=in.Rows.PreviousScroll;c.Cards.Selected=in.Rows.PreviousSelected;c.Cards.Offsets={std::bit_cast<int32_t>(0u-uint32_t(in.ScrollOffset)),std::bit_cast<int32_t>(0u-uint32_t(in.CursorOffset))};}
    else{c.Cards.FirstVisible=in.Rows.Scroll;c.Cards.Selected=in.Rows.Selected;}
  }
  out.push_back(c);
  if(!in.ExternalMask){render(UiRenderKind::AlphaTest,3,false);render(UiRenderKind::DepthTest,5,false);render(UiRenderKind::Stencil,0);}
  return out;
}
}
