#pragma once
#include "gauge.h"
#include "uisprite.h"

namespace Impacto::Saekano {
struct ApproachEntry;
struct ApproachSelectionLayout {
  // Native bac0, bae4 and bb10. Draw consumers deliberately borrow fields
  // across these records and the arrow tables; retain each authored record.
  std::array<UiSpriteDefinition,3> Sprites{};
  std::array<int32_t,2> TextOffset{};
  std::array<int32_t,4> HitRectangle{};
  std::array<std::vector<UiSpriteDefinition>,2> ArrowSprites;
  std::vector<std::array<int32_t,4>> ArrowHitRectangles;
  // bb74/84 parent cards, bb98/a8 selected markers, bbb8/c8 arrows.
  std::array<std::vector<std::array<int32_t,2>>,6> PointTables;
  int32_t SelectedOffset=0;
  std::array<std::array<uint8_t,4>,3> Colors{};
  float TextScale=0;
  std::array<std::array<int32_t,4>,2> Rectangles{};
  std::array<int32_t,2> Offset{};
  size_t PrefixBytes=0;
  static ApproachSelectionLayout Decode(std::span<const uint8_t> bytes);
};
struct ApproachSelectionEnvironment {
  int32_t ScrollDistance=0,ScrollDuration=0;
  static ApproachSelectionEnvironment Decode(std::span<const uint8_t> bytes);
};
struct ApproachSelectionInput {
  bool Previous=false,Next=false,Accept=false,Back=false;
  std::optional<size_t> ParentReleased,ChildReleased;
};
struct ApproachSelectionState {
  bool Started=false,Visible=false,ParentUnselected=true,ChildUnselected=true;
  bool Carousel=false,PreviousArrow=false,NextArrow=false;
  int32_t Phase=0,Parent=0,Child=0,ScrollTarget=-1;
  std::optional<int32_t> PendingPhase;
  float ParentWeight=1;
  ScriptLinearTween Scroll;
  std::vector<size_t> OriginalOrdinals,ChildCounts;
  std::vector<int32_t> Sounds;
};
void StartApproachSelection(ApproachSelectionState& selection,const SemanticState& owner,
    std::span<const ApproachEntry> entries,uint64_t now);
// Hit tests/key polling are explicit host inputs. Flags are read-only here:
// completion publishes a choice for the owning semantic commit in the same update.
bool UpdateApproachSelection(ApproachSelectionState& selection,
    const ApproachSelectionEnvironment& environment,std::span<const uint8_t,18> used,
    const ApproachSelectionInput& input,uint64_t now);
std::optional<std::array<size_t,2>> CompletedApproachChoice(const ApproachSelectionState& selection);
}
