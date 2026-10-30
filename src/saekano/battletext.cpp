#include "battletext.h"
#include "commands.h"
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t Subtract(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
}
void BattleChoiceText::Prepare(std::u16string_view source,int32_t width,const TextFormatEnvironment& format,
    const OrdinaryTextEnvironment& shared,const GsfFont& font,const TextLayoutMetrics& metrics,
    uint32_t utilitySize,uint32_t nominalSize,uint64_t now){
  BattleChoiceText next;
  next.Reveal.PrepareUtf16(source,format,now,true);
  next.Layout=LayoutPlainText(next.Reveal.Text,format,font,metrics,width,shared.BodyScale,utilitySize,nominalSize);
  if(next.Layout.Glyphs.size()!=next.Reveal.Glyphs.size())
    throw UnsupportedCommand("battle choice layout and prepared glyphs disagree");
  next.Reveal.Reveal(now); // Native17dc8, no character/voice reveal timer.
  *this=std::move(next);
}
std::array<int32_t,2> BattleChoiceTextOrigin(const BattleSelectionEnvironment& e,
    size_t control,const PlainTextLayout& layout){
  const auto position=e.Positions.at(control),offset=e.LineOffsets[layout.LineCount==1?1:0];
  return {Subtract(Add(position[0],offset[0]),int32_t(layout.Width/2)),Add(position[1],offset[1])};
}
std::vector<UiTextBlit> BuildBattleChoiceTextDraws(const BattleChoiceText& text,
    const BattleSelectionEnvironment& e,const SharedSelectionEnvironment& selection,
    const TextFormatEnvironment& format,const GsfFont& font,uint32_t utilitySize,uint32_t nominalSize,
    size_t control,uint8_t opacity,bool historyColors,bool suppressed,bool selectedPreviously){
  if(!text.Reveal.Prepared || !text.Reveal.Revealed)throw UnsupportedCommand("battle choice text was not prepared and fully revealed");
  const auto color=selection.TextColors[historyColors && !suppressed && selectedPreviously?1:0];
  auto out=BuildRevealingTextDraws(text.Layout,text.Reveal,format,BattleChoiceTextOrigin(e,control,text.Layout),
      utilitySize,color,opacity,false);
  const float pixels=float(utilitySize)*e.LabelScale;
  if(!std::isfinite(pixels) || pixels<0 || double(pixels)>UINT32_MAX)
    throw FormatError("battle choice label size outside unsigned range");
  const auto size=uint32_t(pixels);const auto& label=e.Labels.at(control);
  const auto width=MeasurePvfText(font,label,size,nominalSize);
  const auto position=e.Positions.at(control),offset=e.LabelPositions.at(control);
  out.push_back({label,{Subtract(Add(position[0],offset[0]),int32_t(width/2)),
      Subtract(Add(position[1],offset[1]),int32_t(size/2))},size,{255,255,255,opacity}});
  return out;
}
}
