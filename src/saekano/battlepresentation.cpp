#include "battlepresentation.h"
#include "commands.h"

namespace Impacto::Saekano {
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
