#include "battledialogue.h"
#include <bit>

namespace Impacto::Saekano {
std::vector<UiSpriteBlit> BuildBattleDialogueBubble(const BattleDialogueEnvironment& e,
    size_t side,uint32_t type,uint32_t openness,uint8_t opacity,std::array<int32_t,2> quake) {
  if(side>1)throw FormatError("battle bubble side outside0/1");
  if(type>2)return {};
  const auto add=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
  auto sprite=e.Bubble;sprite.X=add(e.Positions[side][0],quake[0]);sprite.Y=add(e.Positions[side][1],quake[1]);
  const auto alpha=uint8_t((uint32_t(opacity)*255/11)*openness/100);
  constexpr std::array<uint32_t,3> fill{4,1,7},frame{5,2,8};
  sprite.Color={255,255,255,alpha};auto body=ResolveUiSprite({ProfileTextureSection::BattleUi,-1,fill[type]},sprite,e.Scale,e.Scale);
  sprite.Color={0,0,0,alpha};auto border=ResolveUiSprite({ProfileTextureSection::BattleUi,-1,frame[type]},sprite,e.Scale,e.Scale);
  return {body,border};
}
std::array<int32_t,2> BattleDialogueTextOrigin(const BattleDialogueEnvironment& e,size_t side,uint16_t lineCount,uint16_t renderedWidth) {
  const auto base=e.Positions.at(side),offset=e.LineOffsets[lineCount==1?1:0];
  return {std::bit_cast<int32_t>(uint32_t(base[0])+uint32_t(offset[0])-uint32_t(renderedWidth/2)),
      std::bit_cast<int32_t>(uint32_t(base[1])+uint32_t(offset[1]))};
}
}
