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
}  // namespace Impacto::Saekano
