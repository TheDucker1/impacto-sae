#include "titledraw.h"
#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t Sub(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t Percent(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)*uint32_t(b))/100;}
int32_t Truncate(float v){
  if(std::isnan(v))return 0;
  if(v>=2147483648.0f)return INT32_MAX;
  if(v<=-2147483648.0f)return INT32_MIN;
  return int32_t(v);
}
uint32_t Unsigned(float v){
  if(std::isnan(v) || v<=0)return 0;
  if(v>=4294967296.0f)return UINT32_MAX;
  return uint32_t(v);
}
UiSpriteBlit InlineSprite(uint32_t texture,UiSpriteDefinition s,float scale=1){
  const auto width=Truncate(float(s.Width)*scale),height=Truncate(float(s.Height)*scale);
  auto x=s.X,y=s.Y;
  const auto horizontal=s.Alignment%3,vertical=s.Alignment/3;
  if(horizontal==1)x=Sub(x,width/2);else if(horizontal==2)x=Sub(x,width);
  if(vertical==1)y=Sub(y,height/2);else if(vertical==2)y=Sub(y,height);
  return {{ProfileTextureSection::TitleUi,-1,texture},{x,y,width,height},{s.SourceX,s.SourceY,s.Width,s.Height},s.Color};
}
}
TitleDrawInputs TitleDrawFromState(const TitleState& s,int32_t blink,std::array<int32_t,2> angles,uint32_t font){
  TitleDrawInputs in;in.Phase=s.Phase;in.Selected=s.Selected;in.Character=s.VoiceCharacter;
  in.Reveal=s.Reveal.Value.Current;in.LogoMove=s.LogoMove.Value.Current;in.LogoDetail=s.LogoDetail.Value.Current;
  in.MenuMove=s.MenuMove.Value.Current;in.MenuDetail=s.MenuDetail.Value.Current;
  for(size_t i=0;i<4;++i)in.Buttons[i]=s.Buttons[i].Value.Current;
  in.BlinkAlpha=blink;in.AmbientDegrees=angles;in.FontSize=font;return in;
}
std::vector<TitleDrawCommand> BuildTitleDraw(const TitleEnvironment& e,const TitleDrawInputs& in){
  std::vector<TitleDrawCommand> out;
  const auto operation=[&](TitleDrawKind kind,uint32_t index=0){TitleDrawCommand c;c.Kind=kind;c.Index=index;out.push_back(std::move(c));};
  operation(TitleDrawKind::Clear);operation(TitleDrawKind::Blend,1);operation(TitleDrawKind::TransformReset);
  if(in.Phase==25 || in.Phase==2 || in.Phase==18 || in.Phase==15 || in.ResourceDrawBlocked)return out;
  if(in.Character<0 || in.Character>=4)throw FormatError("title draw character variant is invalid");
  operation(TitleDrawKind::TransformReset);
  const auto scalar=[&](uint16_t at){return std::bit_cast<int32_t>(e.Scalars.at(at));};
  const auto sprite=[&](uint32_t id,UiSpriteDefinition s){TitleDrawCommand c;c.Kind=TitleDrawKind::Sprite;c.Sprite=InlineSprite(id,s);out.push_back(std::move(c));};
  const auto alpha=[](UiSpriteDefinition s,int32_t progress){s.Color[3]=uint8_t(Percent(progress,255));return s;};
  sprite(78,alpha(e.Sprites.at(0x4c),in.Reveal));
  sprite(73,alpha(e.Sprites.at(0x70),in.Reveal));
  if(in.Phase>=10 && in.Phase<=21)operation(TitleDrawKind::Effect,68);
  sprite(74,e.Sprites.at(0x194));
  auto ring=alpha(e.Sprites.at(0x200),in.LogoMove);
  const auto tint=e.ColorTables.at(0x1e0).at(size_t(in.Character));
  for(size_t i=0;i<3;++i)ring.Color[i]=tint[i];
  ring.X=Add(ring.X,Percent(scalar(0x250),Sub(100,in.LogoMove)));sprite(65,ring);
  auto strip=alpha(e.Sprites.at(0x254),in.LogoMove);
  strip.X=Add(strip.X,Percent(scalar(0x250),Sub(100,in.LogoMove)));sprite(67,strip);
  const auto circle=e.Sprites.at(0x124);
  for(size_t i=0;i<2;++i){
    const auto scale=std::bit_cast<float>(e.Scalars.at(i==0?0x2b8:0x2b4));
    const auto width=Truncate(float(circle.Width)*scale),height=Truncate(float(circle.Height)*scale);
    UiSpriteBlit blit{{ProfileTextureSection::TitleUi,-1,69},
      {Sub(e.Points.at(0x29c)[0],width/2),Sub(e.Points.at(0x29c)[1],height/2),width,height},
      {circle.SourceX,circle.SourceY,circle.Width,circle.Height},circle.Color};
    blit.Color[3]=uint8_t(Percent(in.LogoMove,255));
    const float product=float(in.AmbientDegrees[i])*std::bit_cast<float>(uint32_t(0x40490fd8));
    blit.Rotation=UiSpriteRotation{product/180.0f,e.Points.at(0x2a4)};
    TitleDrawCommand c;c.Kind=TitleDrawKind::Sprite;c.Sprite=blit;out.push_back(std::move(c));
  }
  auto second=alpha(e.Sprites.at(0x224),in.LogoDetail);
  const auto secondTint=e.ColorTables.at(0x1f0).at(size_t(in.Character));
  for(size_t i=0;i<3;++i)second.Color[i]=secondTint[i];
  second.X=Add(second.X,Percent(scalar(0x250),Sub(100,in.LogoDetail)));sprite(66,second);
  sprite(70,e.Sprites.at(0x278));
  const auto portrait=TitlePortraitTexture(uint32_t(in.Character));
  auto back=alpha(e.SpriteTables.at(0x2d0).at(size_t(in.Character)),in.MenuDetail);
  back.X=Add(back.X,Percent(scalar(0x2ec),Sub(100,in.MenuDetail)));sprite(portrait.Entry,back);
  auto front=alpha(e.SpriteTables.at(0x2c0).at(size_t(in.Character)),in.MenuMove);
  front.X=Add(front.X,Percent(scalar(0x2ec),Sub(100,in.MenuMove)));sprite(portrait.Entry,front);
  for(uint32_t i=0;i<4;++i)operation(TitleDrawKind::Widget,i);
  const auto& positions=e.PointTables.at(0x314);
  if(positions.size()>4 || e.MenuLabels.size()<positions.size())throw FormatError("title menu exceeds its animation/label owner");
  const auto font=Unsigned(float(in.FontSize)*std::bit_cast<float>(e.Scalars.at(0x38c)));
  for(size_t i=0;i<positions.size();++i){
    const auto slide=Percent(scalar(0x39c),Sub(100,in.Buttons[i]));
    auto button=alpha(e.Sprites.at(0x2f0),in.Buttons[i]);button.X=positions[i][0];button.Y=Sub(positions[i][1],slide);sprite(63,button);
    const bool selected=int32_t(i)==in.Selected;
    if(selected){auto cursor=alpha(e.Sprites.at(0x324),in.Buttons[i]);const auto& pos=e.PointTables.at(0x348).at(i);cursor.X=pos[0];cursor.Y=Sub(pos[1],slide);sprite(64,cursor);}
    TitleDrawCommand text;text.Kind=TitleDrawKind::Text;text.Text={e.MenuLabels[i],
      {Add(positions[i][0],scalar(0x388)),Sub(Sub(positions[i][1],slide),std::bit_cast<int32_t>(font>>1))},font,
      e.Colors.at(selected?0x390:0x394)};text.Text.Color[3]=uint8_t(Percent(in.Buttons[i],255));out.push_back(std::move(text));
  }
  if(in.Phase<=5 || in.Phase>=22){
    operation(TitleDrawKind::Widget,4);
    sprite(79,alpha(e.Sprites.at(0xdc),in.Reveal));sprite(80,alpha(e.Sprites.at(0xb8),in.Reveal));
    auto prompt=e.Sprites.at(0x94);prompt.Color[3]=uint8_t(in.BlinkAlpha);sprite(77,prompt);
  }
  sprite(71,e.Sprites.at(0x3a0));return out;
}
}
