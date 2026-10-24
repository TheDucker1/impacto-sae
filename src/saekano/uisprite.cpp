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
