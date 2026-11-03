#include "slotenv.h"
#include "commands.h"
#include <bit>

namespace Impacto::Saekano {
SlotEnvironment SlotEnvironment::Decode(std::span<const uint8_t> bytes){
  if(bytes.size()<3 || bytes.size()>32*1024*1024)throw FormatError("invalid slot environment extent");
  SlotEnvironment e;PayloadReader r(bytes);for(auto& f:e.Flags)f=r.U8();
  const auto colorValue=[&]{const auto v=r.U32();return Color{uint8_t(v>>16),uint8_t(v>>8),uint8_t(v),uint8_t(v>>24)};};
  const auto color=[&](uint16_t at){e.Colors.emplace(at,colorValue());};
  const auto scalar=[&](uint16_t at){e.Scalars.emplace(at,r.U32());};
  const auto point=[&](uint16_t at){e.Points.emplace(at,Point{r.I32(),r.I32()});};
  const auto rectangle=[&](uint16_t at){e.Rectangles.emplace(at,Rectangle{r.I32(),r.I32(),r.I32(),r.I32()});};
  // Native1c1740 uses a signed16 parameter followed by seven words and color.
  const auto sprite=[&](uint16_t at){e.Sprites.emplace(at,UiSpriteDefinition::ReadLegacy(r));};
  const auto string=[&](uint16_t at){e.Strings.emplace(at,r.String());};
  const auto colors=[&](uint16_t at){const auto count=r.U32();r.CheckCount(count,4);auto& table=e.ColorTables[at];table.reserve(count);for(uint32_t i=0;i<count;++i)table.push_back(colorValue());};
  color(0);color(4);color(8);scalar(0xc);scalar(0x10);string(0x14);
  scalar(0x4c);scalar(0x50);scalar(0x54);sprite(0x58);sprite(0x7c);sprite(0xa0);
  rectangle(0xc4);sprite(0xd4);sprite(0xf8);point(0x11c);sprite(0x124);rectangle(0x148);point(0x158);scalar(0x160);scalar(0x164);
  for(uint16_t at=0x168;at<=0x264;at+=36)sprite(at);
  point(0x288);scalar(0x290);point(0x294);point(0x29c);scalar(0x2a4);point(0x2a8);scalar(0x2b0);point(0x2b4);scalar(0x2bc);scalar(0x2c0);point(0x2c4);scalar(0x2cc);scalar(0x2d0);
  sprite(0x2d4);sprite(0x2f8);rectangle(0x31c);scalar(0x32c);sprite(0x330);sprite(0x354);point(0x378);
  for(uint16_t at=0x380;at<=0x3ec;at+=36)sprite(at);
  point(0x410);color(0x418);color(0x41c);colors(0x420);colors(0x430);
  scalar(0x440);scalar(0x444);point(0x448);rectangle(0x450);scalar(0x460);scalar(0x464);
  for(uint16_t at=0x468;at<=0x580;at+=56)string(at);
  const auto count=r.U32();r.CheckCount(count,4);auto& strings=e.StringTables[0x5b8];strings.reserve(count);for(uint32_t i=0;i<count;++i)strings.push_back(r.String());
  for(uint16_t at=0x5c8;at<=0x7f8;at+=56)string(at);
  if(r.Remaining())throw FormatError("unexpected slot environment tail");return e;
}
SlotBrowserSettings SlotEnvironment::BrowserSettings()const{
  const auto signedValue=[&](uint16_t at){return std::bit_cast<int32_t>(Scalars.at(at));};
  return {Points.at(0x158)[1],signedValue(0x440),signedValue(0x460),signedValue(0x464),signedValue(0x54),signedValue(0x50)};
}
}
