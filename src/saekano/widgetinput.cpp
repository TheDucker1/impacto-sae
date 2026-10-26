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
WidgetEventHistory::WidgetEventHistory(uint32_t historyFrames) {
  if (!historyFrames || historyFrames > 65536)
    throw FormatError("invalid widget hold history length");
  HeldHistory.resize(historyFrames);
}
WidgetContactEvents SampleWidgetContact(
    std::array<int32_t, 4> rectangle, const std::optional<WidgetTouch>& current,
    const std::optional<WidgetTouch>& previous, int32_t trackedId,
    std::array<float, 2> velocity) {
  WidgetContactEvents events;
  // Native candidate scans are gated by contact-count growth/shrinkage.
  // Replacing the sole ID without changing the count emits neither event.
  if (current && !previous &&
      WidgetContainsPoint(rectangle, current->Position)) {
    events.PressedId = current->Id;
    trackedId = current->Id;
  }
  if (previous && !current &&
      WidgetContainsPoint(rectangle, previous->Position)) {
    events.Released = true;
    trackedId = -1;
  }
  events.Held = WidgetTrackedInside(rectangle, current, previous, trackedId);
  if (events.Held)
    events.Delta = {int32_t(current->Position[0]) - previous->Position[0],
                    int32_t(current->Position[1]) - previous->Position[1]};
  events.Velocity = velocity;
  return events;
}
void WidgetEventHistory::Update(const WidgetContactEvents& contact) {
  Flags = 0;
  if (contact.PressedId) {
    Flags |= 1;
    TrackedId = *contact.PressedId;
    HistoryIndex = 0;
  }
  if (contact.Released) {
    Flags |= 8;
    TrackedId = -1;
  }
  if (contact.Held) Flags |= 2;
  if (HistoryIndex >= 0) {
    if (size_t(HistoryIndex) >= HeldHistory.size())
      throw FormatError("invalid widget hold history index");
    HeldHistory[size_t(HistoryIndex)] = contact.Held;
    HistoryIndex = int32_t((size_t(HistoryIndex) + 1) % HeldHistory.size());
  }
  bool allHeld = true;
  for (auto value : HeldHistory) allHeld = allHeld && value != 0;
  if (allHeld) {
    Flags |= 4;
    for (auto& value : HeldHistory) value = 0;
    HistoryIndex = -1;
  }
  if (contact.PressedId && DoublePressCounter) {
    Flags |= 0x20;
  } else {
    if (contact.PressedId) DoublePressCounter = 1;
    if (DoublePressCounter) {
      DoublePressCounter <<= 1;
      if (DoublePressCounter >= 0x8000) DoublePressCounter = 0;
    }
  }
  if (contact.Held) {
    Delta = contact.Delta;
    if (Delta[0] || Delta[1]) Flags |= 0x10;
  }
  if (contact.Released) {
    Velocity = contact.Velocity;
    AbsoluteVelocity =
        std::array<float, 2>{std::fabs(Velocity[0]), std::fabs(Velocity[1])};
    if (WidgetReleaseFlick(true, Velocity)) Flags |= 0xc0;
  }
  if (Flags & 0x20) Toggle = !Toggle;
  const auto advance = [](uint32_t counter) {
    if (counter) counter <<= 1;
    return counter >= 0x400 ? 0u : counter;
  };
  if (Flags & 0x40) GestureCounter = 1;
  if (Flags & 0x80) SecondaryGestureCounter = 1;
  GestureCounter = advance(GestureCounter);
  SecondaryGestureCounter = advance(SecondaryGestureCounter);
}
}  // namespace Impacto::Saekano
