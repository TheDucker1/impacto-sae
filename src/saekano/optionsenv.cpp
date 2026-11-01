#include "optionsenv.h"
#include "commands.h"
namespace Impacto::Saekano {
namespace {
template<class Read> auto Table(PayloadReader& r,size_t stride,Read read){
  const auto n=r.U32();r.CheckCount(n,stride);
  std::vector<decltype(read())> values;values.reserve(n);
  for(uint32_t i=0;i<n;++i)values.push_back(read());return values;
}
}
OptionsDebugSettings OptionsDebugSettings::Decode(std::span<const uint8_t> bytes){
  // Three header bytes,20 feature bytes, two scalar words, eleven feature
  // bytes, then the silent flag. No counted collections precede this field.
  constexpr size_t prefix=3+20+8+11+1;
  if(bytes.size()<prefix)throw FormatError("truncated Options debug settings");
  OptionsDebugSettings settings;for(size_t i=0;i<3;++i)settings.Flags[i]=bytes[i];
  // Native reader801c5c64: c240 begins at byte3. The movie constructor
  // reads c245, while CG/music/battle read c242/c243/c244 respectively.
  settings.UnlockMovies=bytes[8];settings.UnlockCg=bytes[5];settings.UnlockMusic=bytes[6];settings.UnlockBattle=bytes[7];
  settings.Silent=bytes[prefix-1];settings.PrefixBytes=prefix;return settings;
}
OptionsEnvironment OptionsEnvironment::Decode(std::span<const uint8_t> bytes){
  if(bytes.size()<3 || bytes.size()>32*1024*1024)throw FormatError("invalid options environment extent");
  PayloadReader r(bytes);OptionsEnvironment e;for(auto& f:e.Flags)f=r.U8();
  const auto scalar=[&](uint16_t at){e.Scalars.emplace(at,r.U32());};
  const auto color=[&](uint16_t at){const auto v=r.U32();e.Colors.emplace(at,std::array<uint8_t,4>{uint8_t(v>>16),uint8_t(v>>8),uint8_t(v),uint8_t(v>>24)});};
  const auto pointValue=[&]{return Point{r.I32(),r.I32()};};
  const auto rectValue=[&]{return Rectangle{r.I32(),r.I32(),r.I32(),r.I32()};};
  const auto spriteValue=[&]{return UiSpriteDefinition::Read(r);};
  const auto point=[&](uint16_t at){e.Points.emplace(at,pointValue());};
  const auto rect=[&](uint16_t at){e.Rectangles.emplace(at,rectValue());};
  const auto sprite=[&](uint16_t at){e.Sprites.emplace(at,spriteValue());};
  const auto points=[&](uint16_t at){e.PointTables.emplace(at,Table(r,8,pointValue));};
  const auto sprites=[&](uint16_t at){e.SpriteTables.emplace(at,Table(r,36,spriteValue));};
  const auto integers=[&](uint16_t at){e.IntegerTables.emplace(at,Table(r,4,[&]{return r.I32();}));};
  const auto pointRows=[&](uint16_t at){e.PointRows.emplace(at,Table(r,4,[&]{return Table(r,8,pointValue);}));};
  const auto rectRows=[&](uint16_t at){e.RectangleRows.emplace(at,Table(r,4,[&]{return Table(r,16,rectValue);}));};
  const auto strings=[&]{return Table(r,4,[&]{return r.String();});};
  const auto stringRowsValue=[&]{return Table(r,4,strings);};
  const auto stringRows=[&](uint16_t at){e.StringRows.emplace(at,stringRowsValue());};
  color(0);color(4);color(8);for(uint16_t at=0xc;at<=0x20;at+=4)scalar(at);
  sprite(0x24);sprite(0x48);sprite(0x6c);points(0x90);sprite(0xa0);points(0xc4);sprite(0xd4);
  sprites(0xf8);rectRows(0x108);sprite(0x118);pointRows(0x13c);stringRows(0x14c);points(0x15c);
  for(uint16_t at=0x16c;at<=0x194;at+=8)point(at);
  sprite(0x19c);pointRows(0x1c0);stringRows(0x1d0);points(0x1e0);
  for(uint16_t at=0x1f0;at<=0x238;at+=8)point(at);
  sprites(0x240);
  for(uint16_t at=0x250;at<=0x304;at+=36)sprite(at);
  scalar(0x328);scalar(0x32c);scalar(0x330);sprite(0x334);sprite(0x358);sprite(0x37c);
  integers(0x3a0);integers(0x3b0);integers(0x3c0);scalar(0x3d0);integers(0x3d4);
  e.StringGrids.emplace(0x3e4,Table(r,4,stringRowsValue));
  sprite(0x3f4);sprite(0x418);sprite(0x43c);sprite(0x460);sprites(0x484);
  rect(0x494);scalar(0x4a4);scalar(0x4a8);scalar(0x4ac);point(0x4b0);
  sprite(0x4b8);sprite(0x4dc);sprite(0x500);rect(0x524);rect(0x534);rect(0x544);
  sprite(0x554);sprite(0x578);point(0x59c);e.SaveQuestion=r.String();
  if(r.Remaining())throw FormatError("unexpected options environment tail");return e;
}
}
