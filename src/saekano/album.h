#pragma once

#include "uisprite.h"
#include <map>

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
// The counted CG section precedes movie data in env_omake. Retain it for the
// album owner instead of skipping a fixed byte count to reach movie records.
// Keys identify native configuration fields, not serialized file offsets.
struct ExtrasScreenEnvironment {
  struct Caption {std::array<int32_t,4> Words{};std::u16string Text;};
  struct Movie {std::u16string Name,Title;};
  AlbumEnvironment Common;
  std::map<uint16_t,uint32_t> Scalars;
  std::map<uint16_t,std::array<int32_t,2>> Points;
  std::map<uint16_t,std::array<int32_t,4>> Rectangles;
  std::map<uint16_t,UiSpriteDefinition> Sprites;
  std::map<uint16_t,std::u16string> Strings;
  std::map<uint16_t,std::vector<UiSpriteDefinition>> SpriteTables;
  std::vector<std::array<int32_t,4>> AlbumRectangles;
  std::vector<Caption> AlbumCaptions;
  std::vector<Movie> Movies;
  size_t PrefixBytes=0;
  // Reader801d1522..1e0a, through movie preview animation period dad0.
  static ExtrasScreenEnvironment DecodeThroughMovies(std::span<const uint8_t> bytes);
};
}  // namespace Impacto::Saekano
