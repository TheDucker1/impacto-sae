#pragma once

#include "formats.h"

#include <array>

namespace Impacto::Saekano {
struct NativeSavedFillRect {
  uint32_t PackedColor = 0;
  int32_t Opacity = 0;
  uint32_t Bucket = 0;
};
struct NativeSavedFloatImageSlot {
  uint8_t Flag = 0;
  int16_t Texture = -1;
  std::array<uint8_t, 3> Parameters{};
  // Native slot+8,+c,+10,+14,+18,+1c,+20, retained as bits. Unused
  // fields can contain uninitialized data; interpretation belongs to consumers.
  std::array<uint32_t, 7> Words{};
};
struct NativeSavedIntegerImageSlot {
  uint8_t Flag = 0;
  std::array<int16_t, 7> GeometryAndTexture{
      0, 0, 0, 0, 0, 0, -1};  // x,y,four geometry words,texture.
  uint8_t Parameter4 = 0;
  int16_t Parameter5 = 0;
  uint8_t Parameter6 = 0;
  std::array<uint32_t, 5> Words{};  // Native+18,+1c,+20,+24,+28.
};
struct NativeImageSavePrefix {
  std::array<NativeSavedFillRect, 4> FillRects;
  std::array<std::array<NativeSavedFloatImageSlot, 4>, 2> FloatSlots;
  std::array<NativeSavedIntegerImageSlot, 10> IntegerSlots;
  std::array<std::vector<uint32_t>, 3> TextureReferences;
  size_t EndOffset = 0;
};
// Native serializer80195ac0 prefix, before unrelated context/owner/model data.
// This is not a decoder for an arbitrary full desktop/native save file.
NativeImageSavePrefix DecodeNativeImageSavePrefix(
    std::span<const uint8_t> bytes);
}  // namespace Impacto::Saekano
