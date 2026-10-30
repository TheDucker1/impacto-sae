#include "title.h"
#include "commands.h"
#include <bit>

namespace Impacto::Saekano {
TitleEnvironment TitleEnvironment::Decode(std::span<const uint8_t> bytes) {
  if(bytes.size()<3 || bytes.size()>32*1024*1024)throw FormatError("invalid title environment extent");
  TitleEnvironment e;PayloadReader r(bytes);
  for(auto& flag:e.Flags)flag=r.U8();
  const auto scalar=[&](uint16_t at){e.Scalars.emplace(at,r.U32());};
  const auto scalars=[&](uint16_t at,size_t n){for(size_t i=0;i<n;++i)scalar(uint16_t(at+i*4));};
  const auto colorValue=[&]{const auto v=r.U32();return std::array<uint8_t,4>{uint8_t(v>>16),uint8_t(v>>8),uint8_t(v),uint8_t(v>>24)};};
  const auto color=[&](uint16_t at){e.Colors.emplace(at,colorValue());};
  const auto pointValue=[&]{return std::array<int32_t,2>{r.I32(),r.I32()};};
  const auto rectValue=[&]{return std::array<int32_t,4>{r.I32(),r.I32(),r.I32(),r.I32()};};
  const auto spriteValue=[&]{return UiSpriteDefinition::ReadLegacy(r);};
  const auto point=[&](uint16_t at){e.Points.emplace(at,pointValue());};
  const auto rect=[&](uint16_t at){e.Rectangles.emplace(at,rectValue());};
  const auto sprite=[&](uint16_t at){e.Sprites.emplace(at,spriteValue());};
  const auto table=[&](auto& tables,uint16_t at,size_t stride,auto read){
    const auto n=r.U32();r.CheckCount(n,stride);auto& values=tables[at];
    for(uint32_t i=0;i<n;++i)values.push_back(read());
  };
  const auto integers=[&](uint16_t at){table(e.IntegerTables,at,4,[&]{return r.I32();});};
  const auto colors=[&](uint16_t at){table(e.ColorTables,at,4,colorValue);};
  const auto points=[&](uint16_t at){table(e.PointTables,at,8,pointValue);};
  const auto rects=[&](uint16_t at){table(e.RectangleTables,at,16,rectValue);};
  const auto sprites=[&](uint16_t at){table(e.SpriteTables,at,34,spriteValue);};
  integers(0);point(0x10);scalar(0x18);
  color(0x1c);color(0x20);color(0x24);scalars(0x28,2);color(0x30);scalar(0x34);
  color(0x38);scalars(0x3c,2);color(0x44);color(0x48);
  for(const uint16_t at:{0x4c,0x70,0x94,0xb8,0xdc,0x100,0x124})sprite(at);
  scalars(0x148,11);colors(0x174);integers(0x184);sprite(0x194);
  scalar(0x1b8);sprite(0x1bc);colors(0x1e0);colors(0x1f0);
  sprite(0x200);sprite(0x224);scalars(0x248,3);
  sprite(0x254);sprite(0x278);point(0x29c);point(0x2a4);scalars(0x2ac,5);
  sprites(0x2c0);sprites(0x2d0);scalars(0x2e0,4);sprite(0x2f0);
  points(0x314);sprite(0x324);points(0x348);rects(0x358);rect(0x368);
  const auto labels=r.U32();r.CheckCount(labels,4);for(uint32_t i=0;i<labels;++i)e.MenuLabels.push_back(r.String());
  scalars(0x388,2);color(0x390);color(0x394);scalars(0x398,2);sprite(0x3a0);
  rect(0x3c4);point(0x3d4);rect(0x3dc);point(0x3ec);rect(0x3f4);point(0x404);rect(0x40c);point(0x41c);
  integers(0x424);rects(0x434);points(0x444);scalars(0x454,3);
  if(r.Remaining())throw FormatError("unexpected title environment tail");
  return e;
}
}
