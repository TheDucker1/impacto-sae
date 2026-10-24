#pragma once

#include "image.h"
#include <compare>

namespace Impacto::Saekano {
// Native root3 child sections; character indexes address section2's archive
// packs, not the current scene's UI ordinal. Names are metadata, never keys.
enum class ProfileTextureSection : uint32_t {
  Portraits = 0,
  Character = 2,
  Interface = 3
};
struct ProfileTextureKey {
  ProfileTextureSection Section;
  int32_t Character = -1;
  uint32_t Entry = 0;
  auto operator<=>(const ProfileTextureKey&) const = default;
};
struct ProfileMenuAsset {
  ProfileTextureKey Key;
  Bytes Name, Data;
};
struct PreparedProfileTexture {
  ProfileTextureKey Key;
  Bytes Name;
  ImagePixels Pixels;
};
}  // namespace Impacto::Saekano
