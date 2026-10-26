#include "widgetinput.h"
#include "formats.h"

#include <bit>
#include <cmath>

namespace Impacto::Saekano {
bool WidgetContainsPoint(std::array<int32_t, 4> rectangle,
                         std::array<int16_t, 2> point) {
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  return point[0] < add(rectangle[0], rectangle[2]) &&
         int32_t(point[0]) + 1 > rectangle[0] &&
         point[1] < add(rectangle[1], rectangle[3]) &&
         int32_t(point[1]) + 1 > rectangle[1];
}
bool WidgetTrackedInside(std::array<int32_t, 4> rectangle,
                         const std::optional<WidgetTouch>& current,
                         const std::optional<WidgetTouch>& previous,
                         int32_t trackedId) {
  return current && previous && trackedId == current->Id &&
         trackedId == previous->Id &&
         WidgetContainsPoint(rectangle, previous->Position);
}
std::array<float, 2> WidgetHistoryVelocity(
    std::span<const WidgetHistoryFrame> frames, uint32_t count,
    uint32_t oldest) {
  if (frames.empty() || frames.size() > uint32_t(INT32_MAX) ||
      count > frames.size() || oldest >= frames.size())
    throw FormatError("invalid widget contact history ring");
  if (count != frames.size()) return {};
  const auto& first = frames[oldest];
  const auto& last = frames[(oldest + frames.size() - 1) % frames.size()];
  if (first.Contacts != 1 || last.Contacts != 1) return {};
  const float inverse = 1.0f / float(frames.size());
  return {float(int32_t(last.Position[0]) - first.Position[0]) * inverse,
          float(int32_t(last.Position[1]) - first.Position[1]) * inverse};
}
bool WidgetReleaseFlick(bool released, std::array<float, 2> velocity) {
  return released &&
         (std::fabs(velocity[0]) > 5.0f || std::fabs(velocity[1]) > 5.0f);
}
}  // namespace Impacto::Saekano
