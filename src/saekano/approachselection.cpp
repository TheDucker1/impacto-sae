#include "approachselection.h"
#include "commands.h"

namespace Impacto::Saekano {
void StartApproachSelection(ApproachSelectionState& s,const SemanticState& owner,
    std::span<const ApproachEntry> entries,uint64_t now) {
  ApproachSelectionState next;
  next.Phase=s.Phase;next.Child=s.Child; // Reset leaves the hidden child navigator intact.
  for(size_t i=0;i<entries.size();++i) {
    if(EvaluateNumeric(owner,entries[i].EnabledExpression)!=1)continue;
    if(i>=6 || entries[i].Children.empty() || entries[i].Children.size()>3)
      throw FormatError("approach menu exceeds original parent/child flag layout");
    next.OriginalOrdinals.push_back(i);next.ChildCounts.push_back(entries[i].Children.size());
  }
  next.Started=next.Visible=true;next.PendingPhase=0;
  next.Scroll.Start(0,0,0,now);s=std::move(next);
}
bool UpdateApproachSelection(ApproachSelectionState& s,const ApproachSelectionEnvironment& e,
    std::span<const uint8_t,18> used,const ApproachSelectionInput& in,uint64_t now) {
  if(!s.Started)throw UnsupportedCommand("approach selection Start was suppressed");
  if(e.ScrollDistance<0 || e.ScrollDuration<0)throw FormatError("invalid approach scroll settings");
  s.Sounds.clear();
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  const auto count=int32_t(s.OriginalOrdinals.size());
  if(s.Phase==0 || s.Phase==1 || s.Phase==2)s.Phase=3;
  if(s.Phase==3) {
    s.Carousel=count>=5;s.Parent=s.Carousel?0:-1;s.Phase=4;
  }
  if(s.Phase==4) {
    s.ParentWeight=1;
    // Empty authored menus remain waiting. Never reproduce native indexing
    // before an empty collection when a direction/accept key is pressed.
    if(count) {
      if(s.ParentUnselected) {
        if(in.Previous || in.Next) {
          s.Sounds.push_back(2);if(!s.Carousel)s.Parent=in.Previous?count-1:0;
          s.ParentUnselected=false;
        } else if(in.ParentReleased && *in.ParentReleased<size_t(count)) {
          s.Sounds.push_back(3);s.Parent=s.Carousel?0:int32_t(*in.ParentReleased);
          s.ParentUnselected=false;s.PendingPhase=6;
        }
      } else if(in.Previous || in.Next) {
        s.Sounds.push_back(2);
        if(s.Carousel) {
          s.ScrollTarget=(s.Parent+count+(in.Previous?-1:1))%count;
          s.Scroll.Start(0,in.Previous?e.ScrollDistance:-e.ScrollDistance,e.ScrollDuration,now);
          s.Phase=5;
        } else s.Parent=(s.Parent+count+(in.Previous?-1:1))%count;
        (in.Previous?s.PreviousArrow:s.NextArrow)=true;
      } else if(in.Accept) {
        s.Sounds.push_back(3);s.Phase=6;
      } else if(in.ParentReleased && *in.ParentReleased<size_t(count)) {
        s.Sounds.push_back(3);if(!s.Carousel)s.Parent=int32_t(*in.ParentReleased);
        s.PendingPhase=6;
      }
    }
  }
  if(s.Phase==5) {
    s.Scroll.Tick(now);
    if(s.Scroll.Finished()) {
      s.Scroll.Start(0,0,0,now);s.Parent=s.ScrollTarget;s.Phase=4;
      s.PreviousArrow=s.NextArrow=false;
    }
  }
  if(s.Phase==6) {
    if(s.Parent<0 || s.Parent>=count)throw FormatError("approach parent selection is invalid");
    s.ParentWeight=0.5f;s.ChildUnselected=true;s.Child=-1;s.PendingPhase=7;
  }
  if(s.Phase==7) {
    const auto children=int32_t(s.ChildCounts.at(size_t(s.Parent)));
    const auto acceptChild=[&](size_t child) {
      const auto index=s.OriginalOrdinals.at(size_t(s.Parent))*3+child;
      if(used[index])s.Sounds.push_back(5);
      else {s.Sounds.push_back(3);s.Child=int32_t(child);s.PendingPhase=9;}
    };
    if(s.ChildUnselected) {
      if(in.Previous || in.Next) {
        s.Sounds.push_back(2);s.Child=in.Previous?children-1:0;s.ChildUnselected=false;
      } else if(in.Back) {s.Sounds.push_back(4);s.PendingPhase=4;}
      else if(in.ChildReleased && *in.ChildReleased<size_t(children)) {
        acceptChild(*in.ChildReleased);s.ChildUnselected=false;
      }
    } else if(in.Previous || in.Next) {
      s.Sounds.push_back(2);s.Child+=in.Previous?-1:1;
      if(s.Child<0)s.Child=children-1;else if(s.Child>=children)s.Child=0;
    } else if(in.Back) {s.Sounds.push_back(4);s.PendingPhase=4;}
    else if(in.Accept) {
      // A rejected first pointer click on a used child can leave native Child
      // at-1 while clearing its unselected bit. Keep rejection safe until the
      // next valid direction/click instead of reading before the flag array.
      if(s.Child>=0 && s.Child<children)acceptChild(size_t(s.Child));
    } else if(in.ChildReleased && *in.ChildReleased<size_t(children))acceptChild(*in.ChildReleased);
  }
  if(s.Phase==9)s.Phase=10;
  if(s.Phase==10)s.Phase=11;
  if(s.Phase==11){s.Visible=false;return true;}
  return false;
}
std::optional<std::array<size_t,2>> CompletedApproachChoice(const ApproachSelectionState& s) {
  if(!s.Started || s.Phase!=11 || s.Parent<0 || s.Child<0)return {};
  return std::array<size_t,2>{s.OriginalOrdinals.at(size_t(s.Parent)),size_t(s.Child)};
}
}
