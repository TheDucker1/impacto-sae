#include "battlepresentation.h"
#include "commands.h"

namespace Impacto::Saekano {
std::vector<UiSpriteBlit> BuildBattleUnderlay(const BattleVersusVisualEnvironment& e,const SemanticState& state) {
  if(!state.BattleVersus.Visible && !state.BattleResult.Visible)return {};
  const auto height=e.BlackBars[0][3]*state.BattleVersus.Tweens[3].Current/100;
  return {{{},{e.BlackBars[0][0],e.BlackBars[0][1],e.BlackBars[0][2],height},{},{0,0,0,255}},
          {{},{e.BlackBars[1][0],544-height,e.BlackBars[1][2],height},{},{0,0,0,255}}};
}
std::vector<UiSpriteBlit> BuildBattleGaugeDraw(const BattleVersusVisualEnvironment& e,const SemanticState& state) {
  if(state.BattleResult.Visible && state.BattleResult.Outcome!=2)return {};
  if(!state.Battle.Known)throw UnsupportedCommand("battle gauge drawing owner is unknown");
  auto sprite=e.Gauge;
  sprite.X=state.Battle.Display;
  sprite.Height=sprite.Height*state.BattleVersus.Tweens[4].Current/100;
  return {ResolveUiSprite({ProfileTextureSection::BattleUi,-1,0},sprite)};
}
std::vector<UiSpriteBlit> BuildBattleSelectionFrame(const BattleSelectionEnvironment& e,bool enabled,int32_t seconds) {
  if(!enabled)return {};
  if(seconds<0 || seconds>99)throw FormatError("battle countdown exceeds two-digit atlas");
  std::vector<UiSpriteBlit> out;
  constexpr std::array<uint32_t,3> textures{32,11,10};
  for(size_t i=0;i<3;++i)out.push_back(ResolveUiSprite({ProfileTextureSection::BattleUi,-1,textures[i]},e.HudFrames[i]));
  for(size_t i=0;i<2;++i) {
    auto source=e.TimerSource;source[0]+=(i?seconds%10:seconds/10)*e.TimerStride;
    out.push_back({{ProfileTextureSection::BattleUi,-1,25},
      {e.TimerPosition[0]+int32_t(i)*e.TimerAdvance,e.TimerPosition[1],source[2],source[3]},source,{0,0,0,255}});
  }
  return out;
}
BattleSelectionPresentation::BattleSelectionPresentation(const BattleSelectionEnvironment& environment,
    UiSpriteDefinition sharedBubble,uint32_t holdFrames,const SharedSelectionEnvironment& selection,
    const OrdinaryTextEnvironment& text,const TextFormatEnvironment& format,const GsfFont& font,
    TextLayoutMetrics metrics,uint32_t utilitySize,uint32_t nominalSize)
    :environment_(environment),selection_(selection),text_(text),format_(format),font_(font),metrics_(metrics),
      utility_(utilitySize),nominal_(nominalSize),widgets_(sharedBubble,holdFrames){}
void BattleSelectionPresentation::BeginFrame(uint64_t now,WidgetInputHistory::Snapshot contacts,uint32_t faceMask){
  if(faceMask>7)throw FormatError("battle selection face mask outside three direct controls");
  now_=now;sampled_=true;faces_=faceMask;widgets_.BeginFrame(std::move(contacts));
}
void BattleSelectionPresentation::RequireFrame()const{
  if(!sampled_)throw UnsupportedCommand("battle selection platform frame was not supplied");
}
void BattleSelectionPresentation::PrepareRow(size_t row,const BattleSelectionRow& value,int32_t width){
  RequireFrame();texts_.at(row).Prepare(value.Label,width,format_,text_,font_,metrics_,utility_,nominal_,now_);
}
void BattleSelectionPresentation::AddControlState(size_t control,uint32_t texture,std::array<int32_t,2> position,
    float scale,std::array<uint8_t,4> color,bool interactive){
  widgets_.Add(control,texture,position,scale,color,interactive);
}
void BattleSelectionPresentation::SetControlPulse(size_t control,size_t state,const BattleSelectionEnvironment::Pulse& pulse){
  RequireFrame();widgets_.ConfigurePulse(control,state,pulse.From,pulse.To,pulse.Duration,now_);
}
void BattleSelectionPresentation::SetControlOffset(size_t control,int32_t y){
  RequireFrame();widgets_.SetOffset(control,y,now_);
}
bool BattleSelectionPresentation::FacePressed(size_t control){
  RequireFrame();if(control>2)throw FormatError("battle selection face control outside0..2");
  return (faces_&(1u<<control))!=0;
}
std::vector<BattleSelectionDraw> BattleSelectionPresentation::Draw(const BattleSelectionUiState& state,
    bool historyColors,bool suppressed,std::span<const uint8_t> history)const{
  std::vector<BattleSelectionDraw> out;if(!state.Enabled)return out;
  if(!state.Known || state.Rows.empty() || state.Rows.size()>4)
    throw UnsupportedCommand("battle selection draw has no valid owning rows");
  if(!history.empty() && history.size()!=state.Rows.size())throw FormatError("battle selection history row count mismatch");
  for(size_t row=1;row<state.Rows.size();++row){
    const auto control=row-1;const auto alpha=state.Fades[control].Current;
    if(alpha<0 || alpha>255)throw UnsupportedCommand("battle selection fade is outside native byte range");
    for(auto& sprite:widgets_.Draw(control,uint8_t(alpha))){BattleSelectionDraw draw;draw.Sprite=std::move(sprite);out.push_back(std::move(draw));}
    const bool selected=!history.empty() && history[row]!=0;
    for(auto& text:BuildBattleChoiceTextDraws(texts_[row],environment_,selection_,format_,font_,utility_,nominal_,
        control,uint8_t(alpha),historyColors,suppressed,selected)){
      BattleSelectionDraw draw;draw.IsText=true;draw.Text=std::move(text);out.push_back(std::move(draw));
    }
  }
  return out;
}
}
