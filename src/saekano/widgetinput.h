#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

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
// Results of platform contact candidate selection and motion sampling. Geometry
// and polling stay separate so both native snapshots and SDL can supply them.
struct WidgetContactEvents {
  std::optional<uint8_t> PressedId;
  bool Released = false, Held = false;
  std::array<int32_t, 2> Delta{};
  std::array<float, 2> Velocity{};
};
// One-contact mouse/front-touch route. Native ID-set difference selects press/
// release candidates; holding uses the captured ID and previous-frame position.
WidgetContactEvents SampleWidgetContact(
    std::array<int32_t, 4> rectangle, const std::optional<WidgetTouch>& current,
    const std::optional<WidgetTouch>& previous, int32_t trackedId,
    std::array<float, 2> velocity);
struct WidgetEventHistory {
  explicit WidgetEventHistory(uint32_t historyFrames);
  void Update(const WidgetContactEvents& contact);
  uint16_t Flags = 0;
  int32_t TrackedId = -1, HistoryIndex = 0;
  uint32_t DoublePressCounter = 0, GestureCounter = 0,
           SecondaryGestureCounter = 0;
  bool Toggle = false;
  std::array<int32_t, 2> Delta{};
  std::array<float, 2> Velocity{};
  std::optional<std::array<float, 2>>
      AbsoluteVelocity;  // Native leaves these unused until release.
  std::vector<uint8_t> HeldHistory;
};
}  // namespace Impacto::Saekano
