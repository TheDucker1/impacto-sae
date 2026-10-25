#pragma once
#include "formats.h"
#include <array>
#include <string>

namespace Impacto::Saekano {
struct DialogButtonLabels {
  std::u16string Primary, Secondary;
};
// env_dialog reader801cbccc..801cd1ca. Rect24 fields are x,y,sourceX,
// sourceY,width,height, without sprite alignment/color words.
struct DialogEnvironment {
  std::array<uint8_t, 3> Flags{};
  std::array<int32_t, 2> HeaderWords{};
  std::vector<std::array<int32_t, 6>> FrameParts;
  std::array<int32_t, 6> Focus{};
  std::array<std::vector<std::array<int32_t, 2>>, 3> ButtonPositions;
  std::array<std::array<int32_t, 2>, 2> Vectors{};
  std::array<std::array<int32_t, 6>, 2> Rectangles{};
  std::array<int32_t, 2> Offset{};
  std::array<int32_t, 6> Decoration{};
  std::array<int32_t, 2> TextExtent{};
  // c438..c45c, retaining raw words until each consumer is traced.
  std::array<int32_t, 10> Settings{};
  std::array<float, 2> TextScales{};
  std::u16string Separator;
  std::array<std::array<uint8_t, 4>, 4> Colors{};
  std::vector<std::vector<DialogButtonLabels>> ButtonGroups;
  std::array<uint8_t, 4> FinalColor{};
  size_t PrefixBytes = 0;
  static DialogEnvironment Decode(std::span<const uint8_t> bytes);
};
// Separate tail reader: older profile timing fixtures intentionally end before
// this prompt. Reading a shorter fixture never fabricates a confirmation
// string.
std::u16string DecodeProfileConfirmationPrompt(std::span<const uint8_t> bytes);
}  // namespace Impacto::Saekano
