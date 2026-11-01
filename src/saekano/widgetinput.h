#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include "uisprite.h"

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
  bool Enabled = true;  // Fresh componentE0; query gate, not collection gate.
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
enum class WidgetQuery : uint16_t { FullHold = 4, Released = 8, Flick = 0x40 };
// Selected variants only, in row order. All ten component histories are kept
// updated independently; hidden variants do not activate their widget.
bool QueryOrdinaryWidget(std::span<const WidgetEventHistory> components,
                          uint32_t widget, WidgetQuery query,
                          bool condition0 = false, bool condition1 = false);
struct WidgetInputEnvironment {
  uint32_t HoldFrames = 0;
  size_t PrefixBytes = 0;
  static WidgetInputEnvironment DecodeUtility(std::span<const uint8_t> utility);
};
// Fresh platform input owner. BeginFrame shifts snapshots once. Each widget's
// event publication may reset/append movement history; idle frames do not
// append.
struct WidgetInputHistory {
  static constexpr uint32_t Capacity =
      4;  // Original platform constructor98684.
  using Snapshot = std::array<std::optional<WidgetTouch>, 2>;
  Snapshot Current{}, Previous{};
  std::array<std::array<WidgetHistoryFrame, 2>, Capacity> Frames{};
  uint32_t Count = 0, Oldest = 0, Next = 0;
  void BeginFrame(Snapshot snapshot);
  void Publish(uint16_t widgetFlags);
  std::array<float, 2> Velocity(uint32_t channel) const;
};
// Ordinary manager's primary/skip/auto components and separate menu widget.
class OrdinaryWidgetInput {
 public:
  OrdinaryWidgetInput(const OrdinaryMessageEnvironment& environment,
                      uint32_t holdFrames);
  void BeginFrame(WidgetInputHistory::Snapshot snapshot);
  bool UpdatePrimary();
  void UpdateButtons(std::array<int32_t, 2> offset);
  void CancelPointer();  // PC focus/capture loss: discard contacts without
                         // activation.
  bool Query(uint32_t widget, WidgetQuery query, bool condition0 = false,
              bool condition1 = false) const;
  bool MenuReleased() const;
  const WidgetInputHistory& Input() const { return input_; }
  const std::vector<WidgetEventHistory>& Components() const {
    return components_;
  }

 private:
  void UpdateComponent(size_t index, std::array<int32_t, 4> rectangle);
  OrdinaryMessageEnvironment environment_;
  WidgetInputHistory input_;
  std::vector<WidgetEventHistory> components_;
  // Native M150a0: two decorative sprites, then the enabled hit rectangle.
  std::array<WidgetEventHistory, 3> menuComponents_;
};
}  // namespace Impacto::Saekano
