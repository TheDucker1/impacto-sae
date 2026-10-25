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
struct ProfileMenuChromeEnvironment {
  UiSpriteDefinition Frame, Header, Stretch, EarlyOverlay, LateOverlay, Footer,
      Marker;
  std::array<int32_t, 2> StretchSize{}, MarkerStep{};
  std::vector<std::array<int32_t, 2>> MarkerPositions;
};
// Native500ac..50372, after backdrop and before stencil rendering.
std::vector<UiSpriteBlit> BuildProfileMenuChrome(
    const ProfileMenuChromeEnvironment& environment, int32_t phase,
    int32_t resultSlide);
enum class UiRenderKind {
  ResetDepthStencilBackground,
  Stencil,
  AlphaTest,
  DepthTest,
  Rectangle,
  Sprite
};
struct UiRenderCommand {
  UiRenderKind Kind = UiRenderKind::ResetDepthStencilBackground;
  int32_t Mode = 0;
  std::array<int32_t, 4> Rectangle{};
  std::array<uint8_t, 4> Color{};
  UiSpriteBlit Sprite;
};
struct ProfileItemFrameEnvironment {
  UiSpriteDefinition Sprite;
  std::vector<std::array<int32_t, 2>> Positions;
};
struct ProfileItemContentEnvironment {
  UiSpriteDefinition Selector;
  std::vector<UiSpriteDefinition> Categories;
  std::array<int32_t, 2> LabelOffset{};
  float LabelScale = 0;
};
struct ProfileArrowEnvironment {
  std::array<UiSpriteDefinition, 2> Upper, Lower;
};
struct ProfilePortraitEnvironment {
  UiSpriteDefinition Sprite;
  std::array<int32_t, 4> Area{};
};
// Native800bf450..5b4: signed starts/destination ends, unsigned source ends.
std::array<std::array<float, 4>, 4> ResolveUiQuad(
    const UiSpriteBlit& blit, uint32_t textureWidth, uint32_t textureHeight);
struct UiTextBlit {
  std::u16string Text;
  std::array<int32_t, 2> Position{};
  uint32_t Size = 0;
  std::array<uint8_t, 4> Color{};
};
// Native50372..50472, with the factory's initial eight widget positions.
// Subsequent carousel movement must supply its updated widget geometry.
std::vector<UiRenderCommand> BuildProfileItemFramePass(
    const ProfileItemFrameEnvironment& environment, int32_t phase,
    int32_t canvasWidth, int32_t canvasHeight,
    std::span<const std::array<int32_t, 2>> frameOffsets = {});
// Native4ff68..500a8. Caller supplies actual animation state, never an oracle.
std::vector<UiSpriteBlit> BuildProfileBackdrop(
    const ProfileBackdropEnvironment& environment, int32_t waveOffset);
}  // namespace Impacto::Saekano
