#include "systemmenuenv.h"
#include "commands.h"

namespace Impacto::Saekano {
namespace {
template<class Read>
auto ReadTable(PayloadReader& r, size_t stride, Read read) {
  const auto n = r.U32();
  r.CheckCount(n, stride);
  std::vector<decltype(read())> values;
  values.reserve(n);
  for (uint32_t i = 0; i < n; ++i) values.push_back(read());
  return values;
}
}

SystemMenuEnvironment SystemMenuEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3 || bytes.size() > 32 * 1024 * 1024)
    throw FormatError("invalid system menu environment extent");
  SystemMenuEnvironment e;
  PayloadReader r(bytes);
  for (auto& flag : e.Flags) flag = r.U8();
  const auto scalar = [&](uint16_t at) { e.Scalars.emplace(at, r.U32()); };
  const auto colorValue = [&] {
    const auto v = r.U32();
    return Color{uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v), uint8_t(v >> 24)};
  };
  const auto color = [&](uint16_t at) { e.Colors.emplace(at, colorValue()); };
  const auto spriteValue = [&] { return UiSpriteDefinition::ReadLegacy(r); };
  const auto sprite = [&](uint16_t at) { e.Sprites.emplace(at, spriteValue()); };
  const auto point = [&](uint16_t at) { e.Points.emplace(at, Point{r.I32(), r.I32()}); };
  const auto integers = [&] { return ReadTable(r, 4, [&] { return r.I32(); }); };
  const auto integerList = [&](uint16_t at) {
    IntegerList list;
    list.Parameter = r.I32();
    list.Values = integers();
    e.IntegerLists.emplace(at, std::move(list));
  };
  color(0); color(4); color(8);
  sprite(0xc); sprite(0x30); scalar(0x54); sprite(0x58);
  for (uint16_t at = 0x7c; at <= 0x88; at += 4) scalar(at);
  sprite(0x8c); point(0xb0); scalar(0xb8); scalar(0xbc);
  for (uint16_t at = 0xc0; at <= 0xf0; at += 16)
    e.SpriteTables.emplace(at, ReadTable(r, 34, spriteValue));
  scalar(0x100); scalar(0x104); scalar(0x108);
  e.IntegerTables.emplace(0x10c, integers());
  for (uint16_t at = 0x11c; at <= 0x12c; at += 4) scalar(at);
  sprite(0x130); sprite(0x154); sprite(0x178);
  for (uint16_t at = 0x19c; at <= 0x1bc; at += 4) scalar(at);
  sprite(0x1c0); sprite(0x1e4); scalar(0x208); point(0x20c); scalar(0x214);
  sprite(0x218); sprite(0x23c);
  scalar(0x260); scalar(0x264); scalar(0x268);
  e.ColorTables.emplace(0x26c, ReadTable(r, 4, colorValue));
  e.Strings.emplace(0x27c, r.String());
  e.Strings.emplace(0x2b4, r.String());
  scalar(0x2ec); integerList(0x2f0);
  e.WordTables.emplace(0x304, ReadTable(r, 16, [&] {
    return WordRecord{r.I32(), r.I32(), r.I32(), r.I32()};
  }));
  integerList(0x314);
  for (uint16_t at = 0x328; at <= 0x334; at += 4) scalar(at);
  color(0x338); color(0x33c);
  if (r.Remaining()) throw FormatError("unexpected system menu environment tail");
  e.Bytes = bytes.size();
  return e;
}
}
