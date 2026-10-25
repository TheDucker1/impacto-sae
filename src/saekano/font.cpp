#include "font.h"
#include "commands.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
TextLayoutMetrics TextLayoutMetrics::Decode(std::span<const uint8_t> global,
                                            std::span<const uint8_t> message) {
  if (global.size() < 3 || message.size() < 3 ||
      global.size() > 32 * 1024 * 1024 || message.size() > 32 * 1024 * 1024)
    throw FormatError("invalid text layout environment extent");
  TextLayoutMetrics result;
  PayloadReader g(global.subspan(3));
  for (size_t i = 0; i < 4; ++i) g.String();
  g.U32();
  g.U32();
  for (size_t i = 0; i < 9; ++i) g.String();
  result.Height = g.U32();
  result.GlobalPrefixBytes = global.size() - g.Remaining();
  PayloadReader m(message.subspan(3));
  // Three colors, two legacy34 sprites, rect16, fourteen sprites, color,
  // three sprites, rect16 and point8, before the counted word collection.
  constexpr size_t fixed = 3 * 4 + 2 * 34 + 16 + 14 * 34 + 4 + 3 * 34 + 16 + 8;
  for (size_t i = 0; i < fixed / 2; ++i) m.U16();
  const auto count = m.U32();
  m.CheckCount(count, 4);
  for (uint32_t i = 0; i < count; ++i) m.U32();
  // Three points, word, SHORT, two points, rect16 and two floats.
  for (size_t i = 0; i < (3 * 8 + 4 + 2 + 2 * 8 + 16 + 2 * 4) / 2; ++i) m.U16();
  result.Leading = m.I32();
  result.MessagePrefixBytes = message.size() - m.Remaining();
  return result;
}
int32_t TextLayoutMetrics::LineAdvance(float scale) const {
  const float product = float(Height) * scale;
  const float value = product + float(Leading);
  if (std::isnan(value)) return 0;
  if (value >= 2147483648.0f) return INT32_MAX;
  if (value <= -2147483648.0f) return INT32_MIN;
  return int32_t(value);
}
namespace {
uint32_t Word(std::span<const uint8_t> bytes, size_t at) {
  if (at > bytes.size() || bytes.size() - at < 4)
    throw FormatError("truncated GSF header");
  return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 |
         uint32_t(bytes[at + 2]) << 16 | uint32_t(bytes[at + 3]) << 24;
}
uint16_t Short(std::span<const uint8_t> bytes, size_t at) {
  return uint16_t(bytes[at]) | uint16_t(bytes[at + 1]) << 8;
}
int32_t Scaled(uint8_t value, float scale) {
  const float rounded = std::ceil(float(value) * scale);
  const uint32_t converted = std::isnan(rounded) || rounded <= 0 ? 0
                             : rounded >= 4294967296.0f
                                 ? std::numeric_limits<uint32_t>::max()
                                 : uint32_t(rounded);
  return std::bit_cast<int32_t>(converted);
}
}  // namespace
GameUtilityFont GameUtilityFont::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 7) throw FormatError("truncated utility font prefix");
  GameUtilityFont result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  result.Size = std::bit_cast<int32_t>(Word(bytes, 3));
  return result;
}
GsfFont GsfFont::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() > 32 * 1024 * 1024 || Word(bytes, 0) != 0x544e4f46 ||
      Word(bytes, 4) != 0 || Word(bytes, 12) != bytes.size())
    throw FormatError("unsupported GSF header or extent");
  const auto count = Word(bytes, 8);
  if (count < 2 || count > 258 || uint64_t(count) * 8 + 16 > bytes.size())
    throw FormatError("invalid GSF section count");
  std::vector<std::span<const uint8_t>> sections;
  uint64_t end = uint64_t(count) * 8 + 16;
  for (uint32_t i = 0; i < count; ++i) {
    const auto at = Word(bytes, 16 + size_t(i) * 8);
    const auto size = Word(bytes, 20 + size_t(i) * 8);
    if (at < end || uint64_t(at) + size > bytes.size())
      throw FormatError("overlapping or out-of-range GSF section");
    sections.push_back(bytes.subspan(at, size));
    end = uint64_t(at) + size;
  }
  if (end != bytes.size()) throw FormatError("trailing GSF data");
  GsfFont result;
  for (size_t i = 0; i + 2 < sections.size(); ++i)
    result.Atlases.emplace_back(sections[i].begin(), sections[i].end());
  const auto metrics = sections[count - 2], characters = sections[count - 1];
  if (metrics.data() + metrics.size() != characters.data())
    throw FormatError("GSF character section must follow glyph records");
  if (metrics.size() % 10) throw FormatError("partial GSF glyph record");
  for (size_t at = 0; at < metrics.size(); at += 10) {
    const auto character = char16_t(Short(metrics, at));
    GsfGlyph glyph{Short(metrics, at + 2), Short(metrics, at + 4),
                   Short(metrics, at + 6), metrics[at + 8], metrics[at + 9]};
    if (glyph.Atlas >= result.Atlases.size())
      throw FormatError("GSF glyph atlas outside section pool");
    result.Glyphs[character] = glyph;
    // Actual native metadata reader overwrites the shared bitmap height for
    // each record; the last record determines cached-glyph source height.
    result.BitmapHeight = glyph.Height;
  }
  if (characters.size() < 6 || Word(characters, 0) != characters.size() - 4 ||
      characters.size() % 2 || Short(characters, 4) != 0xfeff)
    throw FormatError("invalid GSF UTF16 character section");
  for (size_t at = 6; at < characters.size(); at += 2) {
    const auto character = char16_t(Short(characters, at));
    if (character != u'"' && character != u'\r' && character != u'\n')
      result.Characters.push_back(character);
  }
  return result;
}
uint32_t PvfGlyphAdvance(char16_t character, uint32_t pixelSize) {
  if ((character >= 0x20 && character <= 0x7e) ||
      (character >= 0xff61 && character <= 0xffa0))
    return pixelSize >> 1;
  return pixelSize;
}
uint32_t MeasurePvfText(const GsfFont& font, std::u16string_view text,
                        uint32_t pixelSize, uint32_t nominalSize) {
  if (!nominalSize || nominalSize > uint32_t(INT32_MAX))
    throw FormatError("invalid PVF nominal measurement size");
  const float scale = pixelSize ? float(pixelSize) / float(nominalSize) : 1;
  uint32_t width = 0;
  for (const auto character : text) {
    const bool half = (character >= 0x20 && character <= 0x7e) ||
                      (character >= 0xff61 && character <= 0xffa0);
    const auto found = font.Glyphs.find(character);
    const float base = found == font.Glyphs.end()
                           ? float(nominalSize)
                           : float(found->second.Width);
    const float product = base * scale;
    float next;
    if (half)
      next = float(width) + product * .5f;
    else if (found != font.Glyphs.end()) {
      const float padded = 1.0f + product;
      next = padded + float(width);
    } else
      next = float(width) + product;
    width = std::isnan(next) || next <= 0 ? 0
            : next >= 4294967296.0f       ? UINT32_MAX
                                          : uint32_t(next);
  }
  return width;
}
std::optional<GsfGlyphDraw> ResolveGsfGlyph(const GsfFont& font,
                                            char16_t character, int32_t x,
                                            int32_t y, uint32_t pixelSize) {
  const auto found = font.Glyphs.find(character);
  if (found == font.Glyphs.end()) return {};
  if (!font.BitmapHeight) throw FormatError("zero GSF bitmap height");
  const auto& glyph = found->second;
  const float scale =
      pixelSize ? float(pixelSize) / float(font.BitmapHeight) : 1;
  GsfGlyphDraw result;
  result.Atlas = glyph.Atlas;
  result.Destination = {x, y, Scaled(glyph.Width, scale),
                        Scaled(font.BitmapHeight, scale)};
  result.Source = {glyph.X, glyph.Y, glyph.Width, font.BitmapHeight};
  return result;
}
}  // namespace Impacto::Saekano
