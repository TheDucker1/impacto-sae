#pragma once
#include "formats.h"
#include <array>
#include <optional>

namespace Impacto::Saekano {
// Native records start at owner+2b4+index*1928. Names below retain the
// serializer's owner-relative offsets until their consumers are traced.
struct SlotThumbnail {
  uint32_t Format=4;
  Bytes Pixels;
};
struct NativeSlotRecord {
  std::array<uint8_t,3> Bytes2b6{};
  std::array<uint8_t,4096> Bytesad8{};
  std::array<uint32_t,3> Words1ad8{};
  std::array<std::vector<uint16_t>,3> Strings;
  uint32_t Word1b54=0;
  uint8_t Byte1b90=0;
  std::array<uint32_t,4> Words1b94{};
  uint8_t Byte1ba4=0;
  uint32_t Word1ba8=0;
  std::array<uint16_t,1024> Shorts2d4{};
  std::array<uint32_t,7> Words2b8{};
  std::array<uint32_t,12> Words1bac{};
  uint8_t ThumbnailPresent=0;
  std::optional<SlotThumbnail> Thumbnail;
};
struct NativeSlotData {
  static constexpr size_t FileBytes=0x708000,SlotCount=60;
  // Heap storage avoids a large native-owner-sized stack temporary on x86.
  std::vector<NativeSlotRecord> Records;
  size_t UsedBytes=0;
  static NativeSlotData Decode(std::span<const uint8_t> bytes);
  Bytes Encode()const;
  // Safe zero initialization for four fields the native constructor leaves
  // untouched. Blank format4 thumbnails match actual empty-save startup.
  static NativeSlotData Fresh();
};
}
