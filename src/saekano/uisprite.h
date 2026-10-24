#pragma once

#include "profiletexture.h"

namespace Impacto::Saekano {
class PayloadReader;
struct UiSpriteDefinition {
  int32_t Parameter = 0, Alignment = 0, X = 0, Y = 0;
  int32_t SourceX = 0, SourceY = 0, Width = 0, Height = 0;
  std::array<uint8_t, 4> Color{};
  static UiSpriteDefinition Read(PayloadReader& reader);
};
struct UiSpriteBlit {
  ProfileTextureKey Texture;
  std::array<int32_t, 4> Destination{}, Source{};  // x,y,width,height.
  std::array<uint8_t, 4> Color{};
};
// Native8017c4e0 lowers its scale/alignment to the raw sprite backend call.
UiSpriteBlit ResolveUiSprite(ProfileTextureKey texture,
                             const UiSpriteDefinition& sprite, float scaleX = 1,
                             float scaleY = 1);
struct ProfileBackdropEnvironment {
  int32_t IntroFade = 0, WavePeriod = 0;
  std::array<UiSpriteDefinition, 3> Sprites;
  size_t PrefixBytes = 0;
  static ProfileBackdropEnvironment Decode(std::span<const uint8_t> bytes);
};
// Native4ff68..500a8. Caller supplies actual animation state, never an oracle.
std::vector<UiSpriteBlit> BuildProfileBackdrop(
    const ProfileBackdropEnvironment& environment, int32_t waveOffset);
}  // namespace Impacto::Saekano
