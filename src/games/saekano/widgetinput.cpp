#include "widgetinput.h"
#include "formats.h"
#include "commands.h"

#include <bit>
#include <cmath>
#include <utility>

namespace Impacto::Saekano {
bool QueryOrdinaryWidget(std::span<const WidgetEventHistory> components,
                          uint32_t widget, WidgetQuery query,
                          bool condition0, bool condition1) {
  if (components.size() != 10 || widget > 2)
    throw FormatError("invalid ordinary widget query layout");
  const auto mask = uint16_t(query);
  if (query != WidgetQuery::FullHold && query != WidgetQuery::Released &&
      query != WidgetQuery::Flick)
    throw FormatError("unknown ordinary widget query");
  const auto test = [&](size_t index) {
    return components[index].Enabled && (components[index].Flags & mask) != 0;
  };
  if (widget == 0) return test(0);
  const auto indices = widget == 1
                           ? std::array<size_t, 3>{1, condition0 ? 2u : 3u,
                                                   condition1 ? 4u : 5u}
                           : std::array<size_t, 3>{6, 7, condition0 ? 8u : 9u};
  for (auto index : indices)
    if (test(index)) return true;
  return false;
}

bool OrdinaryWidgetInput::Query(uint32_t widget, WidgetQuery query,
                                bool condition0, bool condition1) const {
  return QueryOrdinaryWidget(components_, widget, query, condition0, condition1);
}

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
WidgetInputEnvironment WidgetInputEnvironment::DecodeUtility(
    std::span<const uint8_t> utility) {
  if (utility.size() < 3) throw FormatError("truncated widget utility flags");
  PayloadReader reader(utility.subspan(3));
  reader.U32();  // Font size.
  const auto count = reader.U32();
  reader.CheckCount(count, 4);
  for (uint32_t i = 0; i < count; ++i) reader.U32();
  if (!reader.Remaining()) throw FormatError("truncated widget debug flag");
  PayloadReader tail(utility.subspan(utility.size() - reader.Remaining() + 1));
  tail.U32();  // Debug tint follows its one-byte flag.
  WidgetInputEnvironment result;
  result.HoldFrames = tail.U32();
  if (!result.HoldFrames || result.HoldFrames > 65536)
    throw FormatError("invalid authored widget hold history length");
  result.PrefixBytes = utility.size() - tail.Remaining();
  return result;
}
void WidgetInputHistory::BeginFrame(Snapshot snapshot) {
  Previous = Current;
  Current = std::move(snapshot);
}
void WidgetInputHistory::Publish(uint16_t widgetFlags) {
  if (widgetFlags & 1) Count = Oldest = Next = 0;
  if (!(widgetFlags & 0x10)) return;
  for (size_t channel = 0; channel < Current.size(); ++channel) {
    const auto& touch = Current[channel];
    Frames[Next][channel] =
        touch ? WidgetHistoryFrame{1, touch->Position} : WidgetHistoryFrame{};
  }
  Next = (Next + 1) % Capacity;
  if (Count < Capacity)
    ++Count;
  else
    Oldest = (Oldest + 1) % Capacity;
}
std::array<float, 2> WidgetInputHistory::Velocity(uint32_t channel) const {
  if (channel >= Current.size())
    throw FormatError("unknown widget input channel");
  std::array<WidgetHistoryFrame, Capacity> samples;
  for (size_t i = 0; i < Capacity; ++i) samples[i] = Frames[i][channel];
  return WidgetHistoryVelocity(samples, Count, Oldest);
}
OrdinaryWidgetInput::OrdinaryWidgetInput(
    const OrdinaryMessageEnvironment& environment, uint32_t holdFrames)
    : environment_(environment),
      menuComponents_{WidgetEventHistory(holdFrames),
                      WidgetEventHistory(holdFrames),
                      WidgetEventHistory(holdFrames)} {
  components_.reserve(10);
  ResetWidgets();
}
void OrdinaryWidgetInput::ResetWidgets() {
  const auto holdFrames=uint32_t(menuComponents_[2].HeldHistory.size());
  components_.clear();
  for (size_t i = 0; i < 10; ++i) components_.emplace_back(holdFrames);
  for(auto& component:menuComponents_)component=WidgetEventHistory(holdFrames);
  menuComponents_[0].Enabled = menuComponents_[1].Enabled = false;
}
void OrdinaryWidgetInput::BeginFrame(WidgetInputHistory::Snapshot snapshot) {
  input_.BeginFrame(std::move(snapshot));
}
void OrdinaryWidgetInput::UpdateComponent(size_t index,
                                          std::array<int32_t, 4> rectangle) {
  auto& component = components_.at(index);
  component.Update(SampleWidgetContact(rectangle, input_.Current[0],
                                       input_.Previous[0], component.TrackedId,
                                       input_.Velocity(0)));
  input_.Publish(component.Flags);
}
bool OrdinaryWidgetInput::UpdatePrimary() {
  UpdateComponent(0, environment_.HitRegion);
  return (components_[0].Flags & 8) != 0;
}
void OrdinaryWidgetInput::UpdateButtons(std::array<int32_t, 2> offset) {
  constexpr std::array<size_t, 9> Indices{0, 2, 1, 4, 3, 5, 6, 8, 7};
  for (size_t i = 0; i < Indices.size(); ++i) {
    const auto sprite = ResolveUiSprite({ProfileTextureSection::MainUi, -1, 3},
                                        environment_.Auxiliary[Indices[i]]);
    auto rectangle = sprite.Destination;
    for (size_t axis = 0; axis < 2; ++axis)
      rectangle[axis] = std::bit_cast<int32_t>(uint32_t(rectangle[axis]) +
                                               uint32_t(offset[axis]));
    UpdateComponent(i + 1, rectangle);
  }
  // Native74d3e collects this separate widget after skip/auto, at offset0,0.
  // Disabled sprite query gates do not suppress their input publications.
  for (size_t i = 0; i < menuComponents_.size(); ++i) {
    auto& component = menuComponents_[i];
    const auto rectangle = i < 2
        ? ResolveUiSprite({ProfileTextureSection::MainUi, -1, 7},
                          environment_.MenuSprites[i]).Destination
        : environment_.MenuHitRegion;
    component.Update(SampleWidgetContact(rectangle, input_.Current[0],
        input_.Previous[0], component.TrackedId, input_.Velocity(0)));
    input_.Publish(component.Flags);
  }
}
bool OrdinaryWidgetInput::MenuReleased() const {
  for (const auto& component : menuComponents_)
    if (component.Enabled && (component.Flags & uint16_t(WidgetQuery::Released)))
      return true;
  return false;
}
void OrdinaryWidgetInput::CancelPointer() {
  input_.Current = input_.Previous = {};
  for (auto& component : components_) {
    component.TrackedId = -1;
    component.Flags = 0;
  }
  for (auto& component : menuComponents_) {
    component.TrackedId = -1;
    component.Flags = 0;
  }
}
}  // namespace Impacto::Saekano
