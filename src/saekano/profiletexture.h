#pragma once

#include "image.h"
#include <compare>

namespace Impacto::Saekano {
// Native root3 child sections, plus separate SharedUi/MainUi/pose namespaces;
// Character-section indexes address section2's archive
// packs, not the current scene's UI ordinal. Names are metadata, never keys.
enum class ProfileTextureSection : uint32_t {
  Portraits = 0,
  Character = 2,
  Interface = 3,
  // Engine namespace for first.dat/root1; distinct from resource.dat/root3.
  SharedUi = 0x100,
  // resource.dat/root1, held by ScenarioGameWrapper+bc for result drawing.
  MainUi = 0x101,
  // Prepared resource.dat/root6 pose textures; Character stores pose ID.
  CharacterPose = 0x102,
  ResponseBackground =
      0x103,          // Prepared scene background; Character is image ID.
  SceneImage = 0x104, // Character stores ImageSection; Entry is original ID.
  BattleLeft = 0x105, BattleRight = 0x106, // Current owned side bank: Character=group, Entry=texture ordinal.
  BattleUi = 0x107 // resource.dat/root16, outer wrapper+cc -> context1c2c.
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
