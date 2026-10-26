#include "messagedraw.h"
#include "commands.h"

#include <bit>
#include <cmath>

namespace Impacto::Saekano {
OrdinaryTextEnvironment OrdinaryTextEnvironment::Decode(
    std::span<const uint8_t> message) {
  OrdinaryTextEnvironment result;
  result.Window = OrdinaryMessageEnvironment::Decode(message);
  PayloadReader reader(message.subspan(result.Window.PrefixBytes));
  reader.U16();
  result.NamePosition = {reader.I32(), reader.I32()};
  for (size_t i = 0; i < 9; ++i)
    reader.U32();  // Other point, rect, scales, leading.
  for (size_t i = 0; i < 5; ++i) reader.U32();  // Other message effect fields.
  result.DefaultScale = reader.U32();
  const auto count = reader.U32();
  reader.CheckCount(count, 4);
  if (!count || count > 4096)
    throw FormatError("invalid message font scale count");
  for (uint32_t i = 0; i < count; ++i)
    result.Scales.push_back(std::bit_cast<float>(reader.U32()));
  if (result.DefaultScale >= result.Scales.size())
    throw FormatError("message default font scale is out of range");
  result.BodyScale = result.Scales[result.DefaultScale];
  result.NameScale = std::bit_cast<float>(reader.U32());
  const auto color = [&]() {
    const auto packed = reader.U32();
    return std::array<uint8_t, 4>{uint8_t(packed >> 16), uint8_t(packed >> 8),
                                  uint8_t(packed), uint8_t(packed >> 24)};
  };
  result.NameColor = color();
  color();  // Separate auxiliary color.
  result.UnreadColor = color();
  result.ReadColor = color();
  if (!std::isfinite(result.BodyScale) || result.BodyScale <= 0 ||
      !std::isfinite(result.NameScale) || result.NameScale <= 0)
    throw FormatError("invalid ordinary text font scale");
  result.PrefixBytes = message.size() - reader.Remaining();
  return result;
}
std::vector<UiTextBlit> BuildOrdinaryMessageTextDraws(
    const OrdinaryTextEnvironment& environment, const OrdinaryMessageText& text,
    const VoiceTextReveal& reveal, const PlainTextLayout& body,
    const TextFormatEnvironment& format, const GsfFont& font,
    uint32_t utilitySize, uint32_t nominalSize, uint32_t openness,
    std::array<int32_t, 2> quake, std::array<uint8_t, 4> primaryColor) {
  if (body.Scale != environment.BodyScale || reveal.Text != text.Body)
    throw FormatError("ordinary text draw inputs belong to different layouts");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto slide =
      std::bit_cast<int32_t>(uint32_t(environment.Window.WindowTravel) *
                             (100u - openness)) /
      100;
  const std::array<int32_t, 2> bodyOrigin{
      add(environment.Window.TextPosition[0], quake[0]),
      add(add(environment.Window.TextPosition[1], slide), quake[1])};
  std::vector<UiTextBlit> draws;
  if (!text.InheritedName && text.NameEnabled && !text.Name.empty()) {
    const auto pixels = float(utilitySize) * environment.NameScale;
    if (!std::isfinite(pixels) || pixels < 0 || pixels >= 2147483648.0f)
      throw FormatError("ordinary name pixel size is out of range");
    const auto size = uint32_t(pixels);
    const auto width = MeasurePvfText(font, text.Name, size, nominalSize);
    auto color = environment.NameColor;
    color[3] = 255;
    draws.push_back(
        {text.Name,
         {add(add(environment.NamePosition[0], quake[0]), -int32_t(width / 2)),
          add(add(add(environment.NamePosition[1], slide), quake[1]),
              -int32_t(size / 2))},
         size,
         color});
  }
  auto glyphs = BuildRevealingTextDraws(body, reveal, format, bodyOrigin,
                                        utilitySize, primaryColor, 255, true);
  draws.insert(draws.end(), glyphs.begin(), glyphs.end());
  return draws;
}
}  // namespace Impacto::Saekano
