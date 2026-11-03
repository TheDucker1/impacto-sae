#pragma once
#include "gauge.h"
#include "uisprite.h"
#include <optional>
#include <functional>

namespace Impacto::Saekano {
struct OrdinaryChoiceInput {
  bool Previous = false, Next = false, Accept = false;
  std::optional<size_t> Released;
};
struct OrdinaryChoiceState {
  bool Started = false, Visible = false, Unselected = true;
  int32_t Phase = 0, Selected = -1;
  std::vector<ScriptLinearTween> Opacity;
  std::vector<uint8_t> Enabled;
  ScriptLinearTween Slide, Hold;
  std::vector<int32_t> Sounds;
};
// Native8d96e/8dfa0: sequential entry fade-in, explicit selection, other-entry
// fade-out, selected-row slide/hold/fade-out. A result is available only at14.
void StartOrdinaryChoice(OrdinaryChoiceState& state, size_t count,
                          const SharedSelectionEnvironment& environment, uint64_t now);
bool UpdateOrdinaryChoice(OrdinaryChoiceState& state,
                           const SharedSelectionEnvironment& environment,
                           const OrdinaryChoiceInput& input, uint64_t now);
std::optional<size_t> CompletedOrdinaryChoice(const OrdinaryChoiceState& state);
struct ChoiceEntry;
struct OrdinaryChoiceDraw {
  bool IsText = false;
  UiSpriteBlit Sprite;
  std::u16string Text;
  std::array<int32_t,2> Position{};
  int32_t FontSize = 0;
  std::array<uint8_t,4> Color{};
};
std::vector<std::array<int32_t,4>> OrdinaryChoiceHitRegions(
    const SharedSelectionEnvironment& environment, const OrdinaryChoiceState& state);
std::vector<OrdinaryChoiceDraw> BuildOrdinaryChoiceDraws(
    const SharedSelectionEnvironment& environment, const OrdinaryChoiceState& state,
    std::span<const ChoiceEntry> entries, std::span<const uint8_t> readHistory,
    int32_t utilityFontSize,
    const std::function<int32_t(const std::u16string&,int32_t)>& measure);
}
