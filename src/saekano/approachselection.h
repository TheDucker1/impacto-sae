#pragma once
#include "gauge.h"

namespace Impacto::Saekano {
struct ApproachEntry;
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
