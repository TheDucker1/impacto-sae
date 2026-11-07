#include "slotdraw.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace Impacto::Saekano {
namespace {
int32_t Wrap(uint32_t v){return std::bit_cast<int32_t>(v);}
int32_t Add(int32_t a,int32_t b){return Wrap(uint32_t(a)+uint32_t(b));}
int32_t Sub(int32_t a,int32_t b){return Wrap(uint32_t(a)-uint32_t(b));}
int32_t Truncate(float v){if(std::isnan(v))return 0;if(v>=2147483648.f)return INT32_MAX;if(v<=-2147483648.f)return INT32_MIN;return int32_t(v);}
int32_t Anchor(int32_t v,int32_t n,int32_t align){return align==1?Sub(v,n/2):align==2?Sub(v,n):v;}
}
SlotDrawInputs SlotDrawFromState(const SlotControllerState& c,const SlotBrowserState& b,bool context,bool question,std::array<int32_t,2> decoration){
  if(!c.Started || !b.Started)throw UnsupportedCommand("slot drawing has no controller/browser owner");
  return {c.Mode,b.Rows.Selected,c.Fade.Value.Current,context,question,decoration};
}
std::vector<SlotDrawCommand> BuildSlotDraw(const SlotEnvironment& e,const SlotDrawInputs& in){
  if(in.Selected<0 || in.Selected>=60)throw FormatError("slot draw selection outside60 records");
  std::vector<SlotDrawCommand> out;
  const auto operation=[&](SlotDrawKind kind,uint32_t index=0){SlotDrawCommand c;c.Kind=kind;c.Index=index;out.push_back(c);};
  const auto rectangle=[&](std::array<uint8_t,4> color){SlotDrawCommand c;c.Kind=SlotDrawKind::Rectangle;c.Rectangle={0,0,960,544};c.Color=color;out.push_back(c);};
  const auto widget=[&](SlotBrowserWidget w,uint32_t i){SlotDrawCommand c;c.Kind=SlotDrawKind::Widget;c.Widget=w;c.Index=i;c.Color={255,255,255,255};out.push_back(c);};
  const auto sprite=[&](ProfileTextureSection section,uint32_t entry,UiSpriteDefinition s,
                        std::optional<SlotEnvironment::Point> extent=std::nullopt,bool aligned=true){
    const auto n=extent.value_or(SlotEnvironment::Point{Truncate(float(s.Width)),Truncate(float(s.Height))});
    SlotDrawCommand c;c.Kind=SlotDrawKind::Sprite;c.Sprite.Texture={section,-1,entry};c.Sprite.Source={s.SourceX,s.SourceY,s.Width,s.Height};c.Sprite.Color=s.Color;
    c.Sprite.Destination={aligned?Anchor(s.X,n[0],s.Alignment%3):s.X,aligned?Anchor(s.Y,n[1],s.Alignment/3):s.Y,n[0],n[1]};out.push_back(c);
  };
  if(in.Mode==0 || in.Mode==1){
    if(in.Context)rectangle({0,0,0,uint8_t(in.Mode==0?128:200)});
    sprite(ProfileTextureSection::SystemUi,69,e.Sprites.at(0x124));
    const uint32_t texture=in.Mode==0?63u:62u;
    sprite(ProfileTextureSection::SystemUi,texture,e.Sprites.at(0xf8),e.Points.at(0x11c));
    sprite(ProfileTextureSection::SystemUi,texture,e.Sprites.at(0xd4));
  }
  for(uint32_t i=0;i<4;++i)widget(SlotBrowserWidget::Activate,i);
  operation(SlotDrawKind::Browser);
  for(uint32_t i=0;i<4;++i)widget(SlotBrowserWidget::Protect,i);
  widget(SlotBrowserWidget::Page,0);
  sprite(ProfileTextureSection::SharedUi,10,e.Sprites.at(0x330));
  sprite(ProfileTextureSection::SharedUi,10,e.Sprites.at(0x354),e.Points.at(0x378));
  sprite(ProfileTextureSection::SharedUi,10,e.Sprites.at(0x380));
  // Native signed-division magic456c797e/asr4 divides by59, the last slot
  // ordinal, so the selected-slot marker reaches the bottom at selection59.
  const auto height=e.Points.at(0x378)[1];const auto step=height/59;const auto thumb=std::max(step,12);
  const auto offset=step<12?Wrap(uint32_t(Sub(height,12))*uint32_t(in.Selected))/59:Wrap(uint32_t(step)*uint32_t(in.Selected));
  const auto origin=Add(e.Sprites.at(0x354).Y,offset),width=e.Points.at(0x410)[0];
  auto s=e.Sprites.at(0x3a4);const auto top=s.Height;s.Y=Add(s.Y,origin);sprite(ProfileTextureSection::SharedUi,11,s,SlotEnvironment::Point{width,top});
  s=e.Sprites.at(0x3c8);const auto middle=Sub(Sub(thumb,top),e.Sprites.at(0x3ec).Height);s.Y=Add(s.Y,origin);sprite(ProfileTextureSection::SharedUi,11,s,SlotEnvironment::Point{width,middle});
  s=e.Sprites.at(0x3ec);s.Y=Add(Add(Add(s.Y,origin),e.Sprites.at(0x3c8).Y),middle);sprite(ProfileTextureSection::SharedUi,11,s,SlotEnvironment::Point{width,top});
  sprite(ProfileTextureSection::SharedUi,6,e.Sprites.at(0x7c),std::nullopt,false);
  if(in.Mode==0 || in.Mode==1){operation(SlotDrawKind::Decoration,uint32_t(in.Mode));out.back().Rectangle={in.DecorationPosition[0],in.DecorationPosition[1],0,0};}
  widget(SlotBrowserWidget::Back,0);
  if(in.QuestionPresent){operation(SlotDrawKind::Question);out.back().Rectangle={480,272,0,0};out.back().Color={255,255,255,90};}
  operation(SlotDrawKind::Save);out.back().Color={255,255,255,90};
  if(in.FadeAlpha!=0)rectangle({255,255,255,uint8_t(in.FadeAlpha)});
  return out;
}
}
