#include "dialog.h"
#include "commands.h"
#include "profile.h"
#include <algorithm>
#include <bit>

namespace Impacto::Saekano {
DialogEnvironment DialogEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3 || bytes.size() > 32 * 1024 * 1024)
    throw FormatError("invalid dialog environment extent");
  DialogEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  const auto vector = [&](auto& values) {
    for (auto& v : values) v = reader.I32();
  };
  const auto count = [&](size_t size) {
    const auto n = reader.U32();
    reader.CheckCount(n, size);
    return n;
  };
  const auto color = [&]() {
    const auto v = reader.U32();
    return std::array<uint8_t, 4>{uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v),
                                  uint8_t(v >> 24)};
  };
  vector(result.HeaderWords);
  const auto frames = count(24);
  for (uint32_t i = 0; i < frames; ++i) {
    std::array<int32_t, 6> value;
    vector(value);
    result.FrameParts.push_back(value);
  }
  vector(result.Focus);
  for (auto& row : result.ButtonPositions) {
    const auto n = count(8);
    for (uint32_t i = 0; i < n; ++i) {
      std::array<int32_t, 2> v;
      vector(v);
      row.push_back(v);
    }
  }
  for (auto& v : result.Vectors) vector(v);
  for (auto& v : result.Rectangles) vector(v);
  vector(result.Offset);
  vector(result.Decoration);
  vector(result.TextExtent);
  vector(result.Settings);
  for (auto& v : result.TextScales) v = std::bit_cast<float>(reader.U32());
  result.Separator = reader.String();
  for (auto& v : result.Colors) v = color();
  const auto groups = count(4);
  for (uint32_t i = 0; i < groups; ++i) {
    const auto n = count(8);
    std::vector<DialogButtonLabels> group;
    for (uint32_t j = 0; j < n; ++j)
      group.push_back({reader.String(), reader.String()});
    result.ButtonGroups.push_back(std::move(group));
  }
  result.FinalColor = color();
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
std::u16string DecodeProfileConfirmationPrompt(std::span<const uint8_t> bytes) {
  const auto environment = ProfileEnvironment::Decode(bytes);
  PayloadReader reader(bytes.subspan(environment.PrefixBytes));
  // ec80 sprite36, eca4 vector8, ecac/ecb0 words, ecb4 UTF16 string.
  for (size_t i = 0; i < 13; ++i) reader.U32();
  return reader.String();
}
DialogFrameDraws BuildDialogFrame(const DialogEnvironment& environment,
                                  std::array<int32_t, 2> center, int32_t width,
                                  int32_t bodyHeight, int32_t progress,
                                  std::array<uint8_t, 4> overlayColor) {
  if (environment.FrameParts.size() < 9 || width < 0 || bodyHeight < 0 ||
      progress < 0 || progress > 100 || environment.Settings[4] < 0)
    throw FormatError("invalid dialog frame layout");
  const auto& p = environment.FrameParts;
  if (p[0][5] <= 0) throw FormatError("zero dialog tile row height");
  const auto columns = uint32_t(environment.Settings[4]);
  const auto rows = uint32_t(bodyHeight / p[0][5]);
  if ((uint64_t(columns) + 2) * (uint64_t(rows) + 2) > 100000)
    throw FormatError("dialog tile count exceeds drawing budget");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto sub = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
  };
  DialogFrameDraws result;
  result.Extent = {width, add(add(bodyHeight, p[0][5]), p[6][5])};
  result.Origin = {sub(center[0], width / 2),
                   sub(center[1], result.Extent[1] / 2)};
  result.Overlay.Kind = UiRenderKind::Rectangle;
  result.Overlay.Rectangle = {0, 0, 960, 544};
  result.Overlay.Color = overlayColor;
  const auto alpha = uint8_t(progress * 255 / 100);
  const auto tile = [&](size_t index, int32_t x, int32_t y) {
    const auto& part = p[index];
    UiSpriteDefinition sprite;
    sprite.X = x;
    sprite.Y = y;
    sprite.SourceX = part[2];
    sprite.SourceY = part[3];
    sprite.Width = part[4];
    sprite.Height = part[5];
    sprite.Color = {255, 255, 255, alpha};
    result.Tiles.push_back(
        ResolveUiSprite({ProfileTextureSection::SharedUi, -1, 0}, sprite));
  };
  const auto row = [&](size_t first, int32_t y) {
    auto x = result.Origin[0];
    tile(first, x, y);
    // Native body rows start their center from the TOP-left width, even when
    // the body's left tile has a different authored width.
    x = add(x, p[first == 3 ? 0 : first][4]);
    for (uint32_t i = 0; i < columns; ++i) {
      tile(first + 1, x, y);
      x = add(x, p[first + 1][4]);
    }
    tile(first + 2, x, y);
  };
  row(0, result.Origin[1]);
  auto y = add(result.Origin[1], p[2][5]);
  for (uint32_t i = 0; i < rows; ++i) {
    row(3, y);
    y = add(y, p[5][5]);
  }
  row(6, y);
  return result;
}
}  // namespace Impacto::Saekano
