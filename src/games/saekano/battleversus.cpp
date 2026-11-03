#include "battleversus.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
Live2dRecord* Actor(SemanticState& state,size_t side) {
  const auto it=std::find_if(state.Live2d.Records.begin(),state.Live2d.Records.end(),
      [&](const auto& r){return r.LogicalId==state.Battle.SideCharacters[side];});
  return it==state.Live2d.Records.end()?nullptr:&*it;
}
int32_t Integer(float value) {
  if(!std::isfinite(value) || double(value)<std::numeric_limits<int32_t>::min() ||
      double(value)>std::numeric_limits<int32_t>::max())throw FormatError("versus position exceeds integer range");
  return int32_t(value);
}
void Position(Live2dRecord& record,Live2dDriver* models,int32_t x,uint64_t now) {
  if(!record.ResourceId)return;
  if(!models || !record.Position)throw UnsupportedCommand("versus model position is unavailable");
  auto transform=models->CurrentTransform(record);
  transform.Position={float(x),(*record.Position)[1]};models->SetTransform(record,transform,now);
  (*record.Position)[0]=float(x);
}
void Restart(BattleVersusState& state,size_t i,uint64_t now) {
  auto& t=state.Tweens[i];t.StartUnsigned(t.From,t.To,t.Duration,now);state.Enabled[i]=true;
}
}
BattleVersusCommand BattleVersusCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader r(payload);return {{r.String(),r.String()}};
}
void StartBattleVersus(SemanticState& state,const BattleVersusEnvironment& e,
                       Live2dDriver* models,uint64_t now) {
  if(!state.Battle.Known || !state.BattleVersus.Known || !state.Live2d.Known)
    throw UnsupportedCommand("versus owning context is unavailable");
  if(e.Offscreen<0 || e.Offscreen>std::numeric_limits<int32_t>::max()-960)
    throw FormatError("invalid versus offscreen distance");
  BattleVersusState next;next.Known=next.Started=next.Visible=true;next.Text=state.BattleVersus.Text;
  next.Tweens[0].Start(255,0,e.FadeDuration,now);
  next.Tweens[3].Start(0,100,e.FirstDuration,now);
  next.Tweens[4].Start(0,100,e.SecondDuration,now);
  next.Tweens[5].Start(e.TextTravel,0,e.TextDuration,now);
  next.Tweens[6].Start(0,255,e.IconDuration,now);
  // Validate both records before changing either renderer or record target.
  for(size_t side=0;side<2;++side) {
    auto* record=Actor(state,side);if(!record || !record->ResourceId)continue;
    if(!record->Position || !models)throw UnsupportedCommand("versus model position is unavailable");
    next.Actors[side]=true;
    next.Tweens[1+side].Start(side?960+e.Offscreen:-e.Offscreen,Integer((*record->Position)[0]),e.SlideDuration,now);
  }
  state.BattleVersus=std::move(next);state.Battle.Display=e.Display;
  for(size_t side=0;side<2;++side)if(state.BattleVersus.Actors[side])
    Position(*Actor(state,side),models,state.BattleVersus.Tweens[1+side].Current,now);
}
bool UpdateBattleVersus(SemanticState& state,Live2dDriver* models,bool accept,uint64_t now) {
  auto& s=state.BattleVersus;
  if(!s.Started)throw UnsupportedCommand("versus Start was suppressed");
  if(s.Completed)return true;
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  const auto tick=[&](size_t i){s.Tweens[i].Tick(now);return s.Tweens[i].Finished();};
  if(s.Phase==0){Restart(s,0,now);s.Phase=1;}
  if(s.Phase==1){if(tick(0))s.PendingPhase=2;state.ScreenFade=s.Tweens[0].Current;}
  if(s.Phase==2){Restart(s,3,now);s.Phase=3;}
  if(s.Phase==3 && tick(3))s.PendingPhase=4;
  if(s.Phase==4){Restart(s,4,now);s.Phase=5;}
  if(s.Phase==5 && tick(4))s.PendingPhase=6;
  if(s.Phase==6) {
    // Correct the native omitted side1 Restart/Tick. Preserve a valid clock
    // even with no side0 record; absent participants never supply heap state.
    for(size_t side=0;side<2;++side)if(s.Actors[side])Restart(s,1+side,now);
    s.Phase=7;
  }
  if(s.Phase==7) {
    bool done=true;
    for(size_t side=0;side<2;++side)if(s.Actors[side]) {
      done=tick(1+side) && done;
      if(auto* record=Actor(state,side);record && record->ResourceId)
        Position(*record,models,s.Tweens[1+side].Current,now);
    }
    if(done)s.PendingPhase=8;
  }
  if(s.Phase==8){Restart(s,5,now);s.Phase=9;}
  if(s.Phase==9 && tick(5))s.PendingPhase=10;
  if(s.Phase==10){Restart(s,6,now);state.PendingEvents.push_back({GameEventKind::Sound,0x10});s.Phase=11;}
  if(s.Phase==11 && tick(6))s.PendingPhase=12;
  if(s.Phase==12 && accept){s.Visible=false;s.Completed=true;}
  return s.Completed;
}
std::vector<BattleVersusDraw> BuildBattleVersusDraws(const BattleVersusVisualEnvironment& e,
    const BattleVersusState& s,std::array<int32_t,2> sides,uint32_t utilitySize,
    const std::function<int32_t(std::u16string_view,int32_t)>& measure) {
  std::vector<BattleVersusDraw> out;if(!s.Visible)return out;
  const auto sprite=[&](uint32_t entry,const UiSpriteDefinition& def){
    BattleVersusDraw d;d.Sprite=ResolveUiSprite({ProfileTextureSection::BattleUi,-1,entry},def);out.push_back(d);
  };
  for(size_t i=0;i<2;++i){auto panel=e.Sprites[i];panel.Color[3]=uint8_t(s.Tweens[6].Current);sprite(uint32_t(29+i),panel);}
  // Original VCVT truncates. Side0 panel measurement uses literal36 scaled;
  // side1 passes0 for the font's nominal measurement. Visible text uses utility.
  const auto nameSize=Integer(float(utilitySize)*e.NameScale);
  const auto textSize=Integer(float(utilitySize)*e.TextScale);
  const auto panelTextSize=Integer(36.0f*e.TextScale);
  if(nameSize<0 || textSize<0)throw FormatError("invalid versus font size");
  const auto text=[&](const std::u16string& value,int32_t center,int32_t y,int32_t size){
    BattleVersusDraw d;d.IsText=true;
    d.Text={value,{center-(measure(value,size)>>1),y},uint32_t(size),{255,255,255,255}};out.push_back(d);
  };
  for(size_t side=0;side<2;++side) {
    if(sides[side]<0)throw FormatError("negative versus character index");
    const auto character=size_t(sides[side]);const auto dims=e.NameSizes.at(character);
    const auto pos=e.NamePositions.at(side);const auto base=e.TextPositions.at(side);
    const auto dy=s.Tweens[5].Current;const auto width=e.TextPadding+measure(s.Text[side],side?0:panelTextSize);
    auto name=e.Sprites[3];name.Alignment=4;name.X=pos[0]-(side?dims[0]:0);
    name.Y=dy+pos[1]-dims[1]/2;name.Width=dims[0];name.Height=dims[1];name.Color=e.Colors.at(character);
    // Destination stretching is independent of the original atlas source.
    BattleVersusDraw nd;nd.Sprite={{ProfileTextureSection::BattleUi,-1,23},
      {name.X,name.Y,dims[0],dims[1]},
      {e.Sprites[3].SourceX,e.Sprites[3].SourceY,e.Sprites[3].Width,e.Sprites[3].Height},name.Color};out.push_back(nd);
    BattleVersusDraw td;td.Sprite={{ProfileTextureSection::BattleUi,-1,24},
      {base[0]-(side?width:0),dy+base[1]-e.Sprites[2].Height/2,width,e.Sprites[2].Height},
      {e.Sprites[2].SourceX,e.Sprites[2].SourceY,e.Sprites[2].Width,e.Sprites[2].Height},e.Colors.at(character)};out.push_back(td);
    text(e.CharacterNames.at(character),pos[0]+(side?-dims[0]/2:dims[0]/2)+e.NameOffsets.at(character),
        dy+pos[1]-(nameSize>>1),nameSize);
    text(s.Text[side],base[0]+(side?-width/2:width/2),dy+base[1]-(textSize>>1),textSize);
  }
  return out;
}
}
