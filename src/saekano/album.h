#pragma once

#include "uisprite.h"

namespace Impacto::Saekano {
struct AlbumEnvironment {
  uint32_t ImageColumns = 0;
  size_t PrefixBytes = 0;
  // Common Extras configuration, retained in native serialized order. Child
  // album/movie/music/battle environments follow PrefixBytes.
  std::array<uint8_t,3> Flags{};
  std::array<uint32_t,3> ColorWords{};
  std::array<int32_t,2> SharedWords{};
  std::array<UiSpriteDefinition,6> SharedSprites;
  std::array<int32_t,2> StretchSize{};
  std::array<int32_t,4> CancelRectangle{};
  std::array<std::array<int32_t,2>,2> SharedPoints{};
  float SharedScale = 0;
  int32_t SharedValue = 0, FadeDuration = 0;
  std::u16string SavePrompt;
  UiSpriteDefinition Backdrop;
  std::array<UiSpriteDefinition,4> SectionHeaders, Buttons;
  UiSpriteDefinition SelectedMarker;
  int32_t SelectedOffset = 0;
  std::vector<std::vector<std::array<int32_t,4>>> ButtonRectangles;
  std::vector<std::u16string> MenuLabels;
  float MenuTextScale = 0;
  std::array<int32_t,2> MenuTextPosition{};
  static AlbumEnvironment Decode(std::span<const uint8_t> bytes);
};
struct AlbumEntry {
  std::u16string Title;
  int32_t Category = 0;
  std::vector<int32_t> Images;
  // Retain the authored gate values without treating them as image IDs.
  std::array<int32_t,2> Gates{};
};
std::vector<AlbumEntry> DecodeAlbumEntries(std::span<const uint8_t> text,
                                         uint32_t imageColumns);
// Native startup selects first.dat root0 / cginfo.dat entry0 AlbumList.txt.
// Values are immutable configuration, not a replacement for saved CG history.
std::vector<int32_t> DecodeAlbumRequirements(std::span<const uint8_t> text,
                                             uint32_t imageColumns);
struct AlbumConfiguration {
  AlbumEnvironment Environment;
  std::vector<int32_t> RequiredImages;
  std::vector<AlbumEntry> Entries;
};
AlbumConfiguration LoadAlbumConfiguration(Bytes firstDat,
                                          std::span<const uint8_t> environment);
}  // namespace Impacto::Saekano
