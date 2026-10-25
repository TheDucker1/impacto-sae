#include "profilearrow.h"
#include "formats.h"
#include "profile.h"

#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace Impacto::Saekano {
namespace {
int32_t Integer(float value) {
  if (std::isnan(value)) return 0;
  if (value >= 2147483648.0f) return INT32_MAX;
  if (value <= -2147483648.0f) return INT32_MIN;
  return int32_t(value);
}
float Ease(float input) {
  // Actual initializer800522cc: (.25,.1,.25,1). Native8009ebbc uses
  // single-precision bisection with input tolerance0.01, not exact inversion.
  const float lower = input - 0.01f, upper = input + 0.01f;
  float lo = 0, hi = 1;
  for (size_t i = 0; i < 64; ++i) {
    const float midpoint = (lo + hi) * 0.5f;
    const float inverse = 1.0f - midpoint;
    const float three = inverse * 3.0f;
    const float square = midpoint * midpoint;
    const float a = (three * midpoint) * midpoint;
    const float b = (three * inverse) * midpoint;
    const float cube = square * midpoint;
    float x = a * 0.25f;
    x = x + b * 0.25f;
    x = x + cube;
    if (x < lower) {
      lo = midpoint;
      continue;
    }
    if (x > upper) {
      hi = midpoint;
      continue;
    }
    float y = a * 1.0f;
    y = y + b * 0.1f;
    return y + cube;
  }
  throw FormatError("profile arrow ease cannot converge");
}
}  // namespace
void ProfileArrowMotion::Begin(float travel, int32_t duration, uint64_t now) {
  if (!std::isfinite(travel) || duration < 0)
    throw FormatError("invalid profile arrow motion configuration");
  Started = true;
  Current = From = 0;
  To = travel;
  Duration = duration;
  Epoch = now;
  Elapsed = 0;
  Offsets = {};
}
void ProfileArrowMotion::Update(uint64_t now) {
  if (!Started) throw FormatError("profile arrow factory has not completed");
  Offsets = {Integer(-Current), Integer(Current)};
  Elapsed = std::bit_cast<int32_t>(uint32_t(now) - uint32_t(Epoch));
  if (Elapsed >= Duration)
    Current = To;
  else {
    if (Elapsed < 0) throw FormatError("profile arrow clock moved backward");
    const float ratio = float(Elapsed) / float(Duration);
    const float curve = Ease(ratio);
    const float delta = To - From;
    const float product = delta * curve;
    Current = From + product;
  }
  if (Current == To) {
    std::swap(From, To);
    Current = From;
    Epoch = now;
    Elapsed = 0;
  }
}
std::vector<UiSpriteBlit> BuildProfileArrows(
    const ProfileArrowEnvironment& environment,
    const ProfileSelectionState& selection, const ProfileArrowMotion& motion) {
  if (selection.Phase != 6 && selection.Phase != 7) return {};
  if (!motion.Started)
    throw FormatError("profile arrow factory has not completed");
  std::vector<UiSpriteBlit> result;
  for (size_t i = 0; i < 2; ++i) {
    auto sprite =
        (i == 0 ? environment.Upper
                : environment.Lower)[selection.ArrowActive[i] ? 0 : 1];
    sprite.Y = std::bit_cast<int32_t>(uint32_t(sprite.Y) +
                                      uint32_t(motion.Offsets[i]));
    sprite.Color = {255, 255, 255, 255};
    result.push_back(ResolveUiSprite({ProfileTextureSection::Interface, -1, 10},
                                     sprite, 1, i == 0 ? 1.0f : -1.0f));
  }
  return result;
}
}  // namespace Impacto::Saekano
