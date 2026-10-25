#include "uisprite.h"

#include "commands.h"

#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
int32_t Subtract(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
}
int32_t Convert(float value) {
  if (std::isnan(value)) return 0;
  if (value >= 2147483648.0f) return std::numeric_limits<int32_t>::max();
  if (value <= -2147483648.0f) return std::numeric_limits<int32_t>::min();
  return int32_t(value);
}
int32_t Size(int32_t size, float scale) {
  const float product = float(size) * scale;
  return Convert(scale > 0 ? product : -product);
}
int32_t Anchor(int32_t coordinate, int32_t size, int32_t alignment) {
  if (alignment == 1) return Subtract(coordinate, size / 2);
  if (alignment == 2) return Subtract(coordinate, size);
  return coordinate;
}
}  // namespace
std::array<std::array<float, 4>, 4> ResolveUiQuad(
    const UiSpriteBlit& blit, uint32_t textureWidth, uint32_t textureHeight) {
  if (!textureWidth || !textureHeight)
    throw FormatError("zero UI texture dimension");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const float x = float(blit.Destination[0]), y = float(blit.Destination[1]);
  const float right = float(add(blit.Destination[0], blit.Destination[2]));
  const float bottom = float(add(blit.Destination[1], blit.Destination[3]));
  const float u = float(blit.Source[0]) / float(textureWidth),
              v = float(blit.Source[1]) / float(textureHeight);
  const float endU =
      float(uint32_t(blit.Source[0]) + uint32_t(blit.Source[2])) /
      float(textureWidth);
  const float endV =
      float(uint32_t(blit.Source[1]) + uint32_t(blit.Source[3])) /
      float(textureHeight);
  return {{{x, y, u, v},
           {right, y, endU, v},
           {x, bottom, u, endV},
           {right, bottom, endU, endV}}};
}
UiSpriteDefinition UiSpriteDefinition::Read(PayloadReader& reader) {
  UiSpriteDefinition result;
  result.Parameter = reader.I32();
  result.Alignment = reader.I32();
  result.X = reader.I32();
  result.Y = reader.I32();
  result.SourceX = reader.I32();
  result.SourceY = reader.I32();
  result.Width = reader.I32();
  result.Height = reader.I32();
  const auto color = reader.U32();
  result.Color = {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color),
                  uint8_t(color >> 24)};
  return result;
}
UiSpriteBlit ResolveUiSprite(ProfileTextureKey texture,
                             const UiSpriteDefinition& sprite, float scaleX,
                             float scaleY) {
  const auto width = Size(sprite.Width, scaleX),
             height = Size(sprite.Height, scaleY);
  UiSpriteBlit result;
  result.Texture = texture;
  result.Destination = {Anchor(sprite.X, width, sprite.Alignment % 3),
                        Anchor(sprite.Y, height, sprite.Alignment / 3), width,
                        height};
  result.Source = {sprite.SourceX, sprite.SourceY,
                   scaleX > 0 ? sprite.Width : Subtract(0, sprite.Width),
                   scaleY > 0 ? sprite.Height : Subtract(0, sprite.Height)};
  result.Color = sprite.Color;
  return result;
}
ProfileBackdropEnvironment ProfileBackdropEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < 3 || bytes.size() > 32 * 1024 * 1024)
    throw FormatError("invalid profile backdrop environment extent");
  PayloadReader reader(bytes.subspan(3));
  for (size_t i = 0; i < 3; ++i) reader.U32();
  ProfileBackdropEnvironment result;
  result.IntroFade = reader.I32();
  for (auto& sprite : result.Sprites) sprite = UiSpriteDefinition::Read(reader);
  result.WavePeriod = reader.I32();
  result.PrefixBytes = bytes.size() - reader.Remaining();
  if (result.IntroFade < 0 || result.WavePeriod < 0)
    throw FormatError("negative profile backdrop transition duration");
  return result;
}
std::vector<UiSpriteBlit> BuildProfileMenuChrome(
    const ProfileMenuChromeEnvironment& environment, int32_t phase,
    int32_t resultSlide) {
  const auto key = [](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::Interface, -1, entry};
  };
  std::vector<UiSpriteBlit> result;
  if (phase >= 12) {
    if (environment.MarkerPositions.size() < 3)
      throw FormatError(
          "profile result markers require three authored positions");
    for (size_t i = 0; i < 3; ++i) {
      auto sprite = environment.Marker;
      const auto& position = environment.MarkerPositions[i];
      sprite.X = std::bit_cast<int32_t>(
          uint32_t(position[0]) +
          uint32_t(resultSlide) * uint32_t(environment.MarkerStep[0]));
      sprite.Y =
          Subtract(position[1],
                   std::bit_cast<int32_t>(uint32_t(resultSlide) *
                                          uint32_t(environment.MarkerStep[1])));
      result.push_back(ResolveUiSprite(key(17), sprite));
    }
  }
  // Native raw backend receives independent destination size; its source
  // extent remains the authored sprite extent, rather than scaling the source.
  auto stretch = ResolveUiSprite(key(24), environment.Stretch);
  stretch.Destination = {
      Anchor(environment.Stretch.X, environment.StretchSize[0],
             environment.Stretch.Alignment % 3),
      Anchor(environment.Stretch.Y, environment.StretchSize[1],
             environment.Stretch.Alignment / 3),
      environment.StretchSize[0], environment.StretchSize[1]};
  result.push_back(stretch);
  result.push_back(ResolveUiSprite(key(24), environment.Header));
  result.push_back(ResolveUiSprite(key(24), phase <= 5
                                                ? environment.EarlyOverlay
                                                : environment.LateOverlay));
  result.push_back(ResolveUiSprite(key(3), environment.Frame));
  result.push_back(ResolveUiSprite(key(11), environment.Footer));
  return result;
}
std::vector<UiRenderCommand> BuildProfileItemFramePass(
    const ProfileItemFrameEnvironment& environment, int32_t phase,
    int32_t canvasWidth, int32_t canvasHeight,
    std::span<const std::array<int32_t, 2>> frameOffsets) {
  if (canvasWidth <= 0 || canvasHeight <= 0)
    throw FormatError("invalid profile mask canvas extent");
  if (environment.Positions.size() < 8)
    throw FormatError("profile item frames require eight authored positions");
  if (!frameOffsets.empty() && frameOffsets.size() != 8)
    throw FormatError("profile carousel frame offsets require eight rows");
  std::vector<UiRenderCommand> result;
  const auto state = [&](UiRenderKind kind, int32_t mode = 0) {
    UiRenderCommand command;
    command.Kind = kind;
    command.Mode = mode;
    result.push_back(command);
  };
  const auto rectangle = [&](std::array<int32_t, 4> bounds,
                             std::array<uint8_t, 4> color) {
    UiRenderCommand command;
    command.Kind = UiRenderKind::Rectangle;
    command.Rectangle = bounds;
    command.Color = color;
    result.push_back(command);
  };
  state(UiRenderKind::ResetDepthStencilBackground);
  state(UiRenderKind::Stencil, 1);  // Replace with reference1.
  state(UiRenderKind::DepthTest,
        0);  // Never; stencil depth-fail still replaces.
  state(UiRenderKind::AlphaTest, 6);  // Greater than zero.
  rectangle({0, 0, canvasWidth, canvasHeight}, {0, 0, 0, 1});
  state(UiRenderKind::Stencil, 2);  // Replace with reference0.
  rectangle({500, 96, 460, 300}, {255, 255, 255, 255});
  state(UiRenderKind::Stencil, 3);    // Equal to zero, keep.
  state(UiRenderKind::AlphaTest, 3);  // Not equal to zero.
  state(UiRenderKind::DepthTest, 5);  // Less or equal, depth writes enabled.
  for (size_t i = 0; i < 8; ++i) {
    auto sprite = environment.Sprite;
    sprite.X = environment.Positions[i][0];
    sprite.Y = environment.Positions[i][1];
    if (!frameOffsets.empty()) {
      sprite.X = std::bit_cast<int32_t>(uint32_t(sprite.X) +
                                        uint32_t(frameOffsets[i][0]));
      sprite.Y = std::bit_cast<int32_t>(uint32_t(sprite.Y) +
                                        uint32_t(frameOffsets[i][1]));
    }
    // Factory uses white local color, not the descriptor's tint. Widget draw
    // multiplies by the parent's phase-dependent byte color.
    const uint8_t tint = phase <= 5 ? 200 : 255;
    sprite.Color = {tint, tint, tint, 255};
    UiRenderCommand command;
    command.Kind = UiRenderKind::Sprite;
    command.Sprite =
        ResolveUiSprite({ProfileTextureSection::Interface, -1, 18}, sprite);
    result.push_back(command);
  }
  return result;
}
std::vector<UiSpriteBlit> BuildProfileBackdrop(
    const ProfileBackdropEnvironment& environment, int32_t waveOffset) {
  const auto key = [](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::Interface, -1, entry};
  };
  std::vector<UiSpriteBlit> result;
  result.push_back(ResolveUiSprite(key(2), environment.Sprites[0]));
  const int32_t remaining = Subtract(environment.Sprites[1].Width, waveOffset);
  for (size_t i = 1; i < 3; ++i) {
    auto first = environment.Sprites[i];
    first.SourceX = waveOffset;
    first.Width = remaining;
    result.push_back(ResolveUiSprite(key(uint32_t(21 + i)), first));
    auto second = environment.Sprites[i];
    second.X = remaining;  // Native deliberately retains full source width.
    result.push_back(ResolveUiSprite(key(uint32_t(21 + i)), second));
  }
  return result;
}
}  // namespace Impacto::Saekano
