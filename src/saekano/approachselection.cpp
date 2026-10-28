#include "approachselection.h"
#include "commands.h"
#include <bit>
#include <cmath>

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
ApproachSelectionHitRegions BuildApproachSelectionHitRegions(
    const ApproachSelectionLayout& e,const SharedSelectionEnvironment& shared,
    const ApproachSelectionEnvironment& navigation,size_t parents,size_t children) {
  if(parents>6 || children>3)throw FormatError("approach hit regions exceed menu limits");
  const auto add=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
  ApproachSelectionHitRegions result;
  if(parents>=5) {
    result.Parents.push_back({add(e.Offset[0],e.SelectedOffset),
        add(e.Offset[1],std::bit_cast<int32_t>(uint32_t(navigation.ScrollDistance)*2)),
        e.HitRectangle[2],e.HitRectangle[3]});
    result.Arrows={e.ArrowHitRectangles.at(0),e.ArrowHitRectangles.at(1)};
  } else {
    const auto& points=e.PointTables[parents==3?0:1];
    for(size_t i=0;i<parents;++i)result.Parents.push_back({add(points.at(i)[0],e.SelectedOffset),
        points.at(i)[1],e.HitRectangle[2],e.HitRectangle[3]});
  }
  const auto& points=shared.PointTables[children==2?0:1];
  for(size_t i=0;i<children;++i)result.Children.push_back({points.at(i)[0],points.at(i)[1],
      e.Rectangles[0][2],e.Rectangles[0][3]});
  return result;
}
std::vector<ApproachSelectionDraw> BuildApproachSelectionDraws(
    const ApproachSelectionLayout& e,const SharedSelectionEnvironment& shared,
    const ApproachSelectionEnvironment& navigation,const ApproachSelectionState& s,
    std::span<const ApproachEntry> entries,std::span<const uint8_t,18> used,
    std::span<const std::array<uint8_t,4>> history,int32_t utilityFontSize,
    const std::function<int32_t(const std::u16string&,int32_t)>& measure) {
  std::vector<ApproachSelectionDraw> draws;
  if(!s.Started || !s.Visible)return draws;
  if(!measure || utilityFontSize<0 || !std::isfinite(s.ParentWeight) ||
     s.ParentWeight<0 || s.ParentWeight>1)
    throw FormatError("invalid approach selection drawing context");
  const auto add=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
  const auto mul=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)*uint32_t(b));};
  const auto alpha=[&](uint8_t value){return uint8_t(float(value)*s.ParentWeight);};
  const auto read=[&](size_t parent,size_t child){
    if(history.empty())return false;
    if(parent>=history.size())throw FormatError("missing approach read history");
    return history[parent].at(child)!=0;
  };
  const auto ordinal=[&](size_t parent){const auto index=s.OriginalOrdinals.at(parent);
    if(index>=entries.size() || index>=6)throw FormatError("invalid approach drawing ordinal");return index;};
  const auto sprite=[&](uint32_t entry,UiSpriteDefinition definition,std::array<int32_t,2> point) {
    definition.X=point[0];definition.Y=point[1];ApproachSelectionDraw draw;
    draw.Sprite=ResolveUiSprite({ProfileTextureSection::MainUi,-1,entry},definition);
    draws.push_back(std::move(draw));
  };
  const auto text=[&](const std::u16string& value,std::array<int32_t,2> point,int32_t size,
                       std::array<uint8_t,4> color,bool baseline) {
    const auto width=measure(value,size);if(width<0)throw FormatError("negative approach label width");
    ApproachSelectionDraw draw;draw.Kind=ApproachSelectionDrawKind::Text;draw.Text=value;
    draw.Position={add(point[0],-width/2),add(point[1],baseline?-size:0)};
    draw.FontSize=size;draw.Color=color;draws.push_back(std::move(draw));
  };
  const auto parentSize=int32_t(float(utilityFontSize)*e.TextScale);
  const auto childSize=int32_t(float(utilityFontSize)*shared.TextScale);
  const auto firstArrow=[&]() -> const UiSpriteDefinition& {
    if(e.ArrowSprites[0].empty())throw FormatError("empty approach arrow descriptor table");
    return e.ArrowSprites[0][0];
  };
  const auto parent=[&](size_t index,std::array<int32_t,2> point,bool selected) {
    const auto original=ordinal(index);auto card=e.Sprites[1];card.Color[3]=alpha(card.Color[3]);
    point[0]=add(point[0],selected?e.SelectedOffset:0);sprite(selected?31:30,card,point);
    auto color=shared.TextColors[read(original,0)?1:0];color[3]=alpha(255);
    text(entries[original].Label,{add(point[0],e.TextOffset[0]),add(point[1],e.TextOffset[1])},parentSize,color,true);
  };
  const auto marker=[&](std::array<int32_t,2> point) {
    auto definition=e.Sprites[2];definition.Color=firstArrow().Color;definition.Color[3]=alpha(255);
    sprite(37,definition,point);
  };
  const auto count=s.OriginalOrdinals.size();
  if(s.Carousel && count) {
    for(size_t i=0;i<2;++i) {
      const auto state=i?s.NextArrow:s.PreviousArrow;
      auto definition=e.ArrowSprites[i].at(state?1:0);
      definition.Alignment=e.ArrowSprites[i].at(0).Alignment;
      definition.Color=firstArrow().Color;definition.Color[3]=alpha(255);
      sprite(39,definition,e.PointTables[count==3?4:5].at(i));
      if(i)draws.back().Sprite.Source[3]=std::bit_cast<int32_t>(0u-uint32_t(definition.Height));
    }
    ApproachSelectionDraw clip;clip.Kind=ApproachSelectionDrawKind::ClipBegin;
    clip.Clip=e.Rectangles[1];draws.push_back(clip);
    auto index=(s.Parent+int32_t(count)-2)%int32_t(count);
    for(int32_t row=0;row<6;++row) {
      parent(size_t(index),{e.Offset[0],add(add(e.Offset[1],mul(navigation.ScrollDistance,row)),s.Scroll.Current)},
          s.Phase!=5 && !s.ParentUnselected && row==2);
      index=(index+1)%int32_t(count);
    }
    if(s.Phase!=5 && !s.ParentUnselected)marker(e.PointTables[3].at(1));
    clip.Kind=ApproachSelectionDrawKind::ClipEnd;draws.push_back(clip);
  } else {
    const auto table=count==3?0:1;
    for(size_t i=0;i<count;++i) {
      const auto selected=int32_t(i)==s.Parent;
      parent(i,e.PointTables[table].at(i),selected);
      if(selected)marker(e.PointTables[table+2].at(i));
    }
  }
  if(s.Phase>6 && s.Parent>=0 && size_t(s.Parent)<count) {
    const auto original=ordinal(size_t(s.Parent));const auto& children=entries[original].Children;
    const auto& points=shared.PointTables[children.size()==2?0:1];
    for(size_t i=0;i<children.size();++i) {
      if(i>=3)throw FormatError("approach drawing child exceeds flag layout");
      const auto selected=int32_t(i)==s.Child;const auto wasRead=read(original,i+1);
      const auto wasUsed=used[original*3+i]!=0;auto card=shared.Sprites[0];
      card.Alignment=firstArrow().Alignment;card.Color=wasUsed?e.Colors[1]:std::array<uint8_t,4>{255,255,255,255};
      sprite(selected?18:19,card,points.at(i));
      if(selected) {
        auto mark=shared.Sprites[2];const auto point=points.at(i);mark.Alignment=0;
        sprite(20,mark,{add(point[0],mark.X),add(point[1],mark.Y)});
      }
      auto color=shared.TextColors[wasRead?1:0];if(wasRead && wasUsed)color=e.Colors[2];
      const auto point=points.at(i);
      text(children[i].Label,{add(point[0],shared.LayoutWords[4]),add(point[1],shared.LayoutWords[5])},childSize,color,false);
    }
  }
  return draws;
}
}
