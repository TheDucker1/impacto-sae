#include "ordinarychoice.h"
#include "formats.h"
#include <bit>

namespace Impacto::Saekano {
namespace {
const std::vector<std::array<int32_t,2>>& Points(const SharedSelectionEnvironment& e, size_t count) {
  const auto& points = e.PointTables[count == 2 ? 0 : 1];
  if (!count || count > points.size() || e.PointTables[1].size() < 2)
    throw FormatError("ordinary choice exceeds authored point tables");
  for (size_t i = 0; i < 4; ++i)
    if (e.LayoutWords[i] < 0) throw FormatError("negative ordinary choice duration");
  return points;
}
}
void StartOrdinaryChoice(OrdinaryChoiceState& state, size_t count,
                          const SharedSelectionEnvironment& e, uint64_t now) {
  Points(e, count);
  OrdinaryChoiceState next;
  next.Started = next.Visible = true;
  next.Opacity.resize(count); next.Enabled.resize(count);
  for (auto& opacity : next.Opacity) opacity.Start(0, 255, e.LayoutWords[0], now);
  next.Slide.Start(0, 0, 0, now);
  state = std::move(next);
}
bool UpdateOrdinaryChoice(OrdinaryChoiceState& s, const SharedSelectionEnvironment& e,
                           const OrdinaryChoiceInput& in, uint64_t now) {
  if (!s.Started) throw FormatError("ordinary choice has not started");
  const size_t count = s.Opacity.size();
  const auto& points = Points(e, count);
  if (s.Enabled.size() != count || (in.Released && *in.Released >= count))
    throw FormatError("invalid ordinary choice input/state");
  s.Sounds.clear();
  if (s.Phase == 0) s.Phase = 1; // Widget/tween storage is prepared by Start.
  if (s.Phase == 1) {
    s.Selected = -1;
    s.Opacity[0].Start(0, 255, e.LayoutWords[0], now); s.Enabled[0] = 1;
    s.Phase = 2;
  }
  if (s.Phase == 2) {
    bool finished = true;
    for (size_t i = 0; i < count; ++i) {
      if (s.Enabled[i]) s.Opacity[i].Tick(now);
      if (!s.Opacity[i].Finished()) { finished = false; break; }
      if (i+1 < count && !s.Enabled[i+1]) {
        s.Opacity[i+1].Start(0, 255, e.LayoutWords[0], now); s.Enabled[i+1] = 1;
      }
    }
    if (finished) s.Phase = 3;
  }
  if (s.Phase == 3) { s.Unselected = true; s.Phase = 4; }
  if (s.Phase == 4) {
    if (in.Previous || in.Next) {
      s.Sounds.push_back(2);
      if (s.Unselected) s.Selected = in.Previous ? int32_t(count)-1 : 0;
      else s.Selected = (s.Selected + int32_t(count) + (in.Previous ? -1 : 1)) % int32_t(count);
      s.Unselected = false;
    } else if ((!s.Unselected && in.Accept) || in.Released) {
      if (in.Released) { s.Selected = int32_t(*in.Released); s.Unselected = false; }
      if (s.Selected < 0 || size_t(s.Selected) >= count) throw FormatError("ordinary choice selection is invalid");
      s.Sounds.push_back(3); s.Phase = 6;
    }
  }
  if (s.Phase == 6) {
    for (size_t i = 0; i < count; ++i)
      if (int32_t(i) != s.Selected) s.Opacity[i].Start(255, 0, e.LayoutWords[3], now);
    s.Phase = 7;
  }
  if (s.Phase == 7) {
    bool finished = true;
    for (size_t i = 0; i < count; ++i) if (int32_t(i) != s.Selected) {
      s.Opacity[i].Tick(now); finished &= s.Opacity[i].Finished();
    }
    if (finished) s.Phase = 8;
  }
  if (s.Phase == 8) {
    // The native destination uses default-table row1 even for two entries.
    const auto delta = uint32_t(e.PointTables[1][1][1]) - uint32_t(points.at(size_t(s.Selected))[1]);
    s.Slide.Start(0, std::bit_cast<int32_t>(delta), e.LayoutWords[1], now); s.Phase = 9;
  }
  if (s.Phase == 9) {
    s.Slide.Tick(now);
    if (s.Slide.Finished()) s.Phase = 10;
  }
  if (s.Phase == 10) { s.Hold.Start(0, 100, e.LayoutWords[2], now); s.Phase = 11; }
  if (s.Phase == 11) { s.Hold.Tick(now); if (s.Hold.Finished()) s.Phase = 12; }
  if (s.Phase == 12) { s.Opacity.at(size_t(s.Selected)).Start(255, 0, e.LayoutWords[3], now); s.Phase = 13; }
  if (s.Phase == 13) {
    auto& opacity = s.Opacity.at(size_t(s.Selected)); opacity.Tick(now);
    if (opacity.Finished()) s.Phase = 14;
  }
  if (s.Phase == 14) { s.Visible = false; return true; }
  return false;
}
std::optional<size_t> CompletedOrdinaryChoice(const OrdinaryChoiceState& s) {
  if (!s.Started || s.Phase != 14 || s.Selected < 0) return {};
  return size_t(s.Selected);
}
}
