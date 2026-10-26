#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace Impacto::Saekano {
struct WidgetTouch {
  uint8_t Id = 0;
  std::array<int16_t, 2> Position{};
};
bool WidgetContainsPoint(std::array<int32_t, 4> rectangle,
                         std::array<int16_t, 2> point);
// Original one-contact path uses previous coordinates after checking that the
// tracked ID is present in both frames. Not a current-position hover test.
bool WidgetTrackedInside(std::array<int32_t, 4> rectangle,
                         const std::optional<WidgetTouch>& current,
                         const std::optional<WidgetTouch>& previous,
                         int32_t trackedId);
struct WidgetHistoryFrame {
  uint32_t Contacts = 0;
  std::array<int16_t, 2> Position{};
};
// Frames are physical ring slots. Count is the filled extent, Oldest the
// oldest slot; incomplete or multi-contact endpoints publish zero velocity.
std::array<float, 2> WidgetHistoryVelocity(
    std::span<const WidgetHistoryFrame> frames, uint32_t count,
    uint32_t oldest);
bool WidgetReleaseFlick(bool released, std::array<float, 2> velocity);
}  // namespace Impacto::Saekano
