#pragma once

#include "formats.h"

namespace Impacto::Saekano {
struct AlbumEnvironment {
  uint32_t ImageColumns = 0;
  size_t PrefixBytes = 0;
  static AlbumEnvironment Decode(std::span<const uint8_t> bytes);
};
// Native startup selects first.dat root0 / cginfo.dat entry0 AlbumList.txt.
// Values are immutable configuration, not a replacement for saved CG history.
std::vector<int32_t> DecodeAlbumRequirements(std::span<const uint8_t> text,
                                             uint32_t imageColumns);
struct AlbumConfiguration {
  AlbumEnvironment Environment;
  std::vector<int32_t> RequiredImages;
};
AlbumConfiguration LoadAlbumConfiguration(Bytes firstDat,
                                          std::span<const uint8_t> environment);
}  // namespace Impacto::Saekano
