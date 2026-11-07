#include "battlewidgets.h"
#include "commands.h"
#include <bit>
#include <cmath>
#include <algorithm>

namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t Subtract(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t Integer(float value){
  if(std::isnan(value))return 0;
  if(value>=2147483648.0f)return INT32_MAX;
  if(value<=-2147483648.0f)return INT32_MIN;
  return int32_t(value);
}
int32_t Anchor(int32_t value,int32_t size,int32_t alignment){
  return alignment==1?Subtract(value,size/2):alignment==2?Subtract(value,size):value;
}
void Reverse(ScriptLinearTween& tween,uint64_t now){
  tween.StartUnsigned(tween.To,tween.From,tween.Duration,now);
}
}
BattleCardSprite::BattleCardSprite(UiSpriteDefinition shape,ProfileTextureKey texture,float scale,
    bool interactive,uint32_t holdFrames):Shape(shape),Texture(texture),Scale(scale),Input(holdFrames) {
  if(!std::isfinite(scale))throw FormatError("invalid battle card base scale");
  // Native anchors unscaled input geometry before applying drawing scale.
  Shape.X=Anchor(Shape.X,Shape.Width,Shape.Alignment%3);
  Shape.Y=Anchor(Shape.Y,Shape.Height,Shape.Alignment/3);
  Input.Enabled=interactive;
}
void BattleCardSprite::ConfigurePulse(float from,float to,int32_t duration,uint64_t now){
  if(!std::isfinite(from)||!std::isfinite(to))throw FormatError("invalid battle card pulse scale");
  Pulse.Start(Integer(from*100.0f),Integer(to*100.0f),duration,now);Pulsing=true;
}
std::array<int32_t,4> BattleCardSprite::HitRectangle(std::array<int32_t,2> offset)const{
  return {Add(Shape.X,offset[0]),Add(Shape.Y,offset[1]),Shape.Width,Shape.Height};
}
void BattleCardSprite::Update(std::array<int32_t,2> offset,const WidgetContactEvents& contact,uint64_t now){
  Offset=offset;Input.Update(contact);
  if(Pulsing){
    Pulse.Tick(now);if(AuxiliaryEnabled)Auxiliary.Tick(now);
    if(Pulse.Finished())Reverse(Pulse,now);
    if(Auxiliary.Finished())Reverse(Auxiliary,now); // Preserve disabled status.
  }
}
UiSpriteBlit BattleCardSprite::Draw(std::array<uint8_t,4> color)const{
  UiSpriteBlit out;out.Texture=Texture;
  for(size_t i=0;i<4;++i){const float factor=float(color[i])/255.0f;
    out.Color[i]=uint8_t(uint32_t(float(Shape.Color[i])*factor));}
  int32_t x=Add(Shape.X,Offset[0]),y=Add(Shape.Y,Offset[1]);
  const float width=float(Shape.Width),height=float(Shape.Height);
  const float scale=Pulsing?float(Pulse.Current)/100.0f:Scale;
  const float scaledWidth=width*scale,scaledHeight=height*scale;
  const int32_t w=Integer(scale>0?scaledWidth:-scaledWidth),h=Integer(scale>0?scaledHeight:-scaledHeight);
  if(Pulsing){
    const float baseWidth=width*Scale,baseHeight=height*Scale;
    const float centerX=float(x)+baseWidth*0.5f,centerY=float(y)+baseHeight*0.5f;
    x=Subtract(Integer(centerX),w/2);y=Subtract(Integer(centerY),h/2);
  }
  out.Destination={x,y,w,h};
  out.Source={Shape.SourceX,Shape.SourceY,scale>0?Shape.Width:Subtract(0,Shape.Width),scale>0?Shape.Height:Subtract(0,Shape.Height)};
  return out;
}
void BattleSelectionWidgets::Add(size_t control,uint32_t texture,std::array<int32_t,2> position,float scale,
    std::array<uint8_t,4> color,bool interactive){
  auto shape=shape_;shape.X=position[0];shape.Y=position[1];shape.Color=color;
  groups_.at(control).emplace_back(shape,ProfileTextureKey{ProfileTextureSection::BattleUi,-1,texture},scale,interactive,holdFrames_);
}
void BattleSelectionWidgets::ConfigurePulse(size_t control,size_t group,float from,float to,int32_t duration,uint64_t now){
  groups_.at(control).at(group).ConfigurePulse(from,to,duration,now);
}
void BattleSelectionWidgets::SetOffset(size_t control,int32_t y,uint64_t now){
  for(auto& group:groups_.at(control)){
    const std::array<int32_t,2> offset{0,y};
    const auto contact=SampleWidgetContact(group.HitRectangle(offset),input_.Current[0],input_.Previous[0],group.Input.TrackedId,input_.Velocity(0));
    group.Update(offset,contact,now);input_.Publish(group.Input.Flags);
  }
}
bool BattleSelectionWidgets::Pressed(size_t control)const{
  for(const auto& group:groups_.at(control))if(group.Pressed())return true;return false;
}
std::vector<UiSpriteBlit> BattleSelectionWidgets::Draw(size_t control,uint8_t opacity)const{
  std::vector<UiSpriteBlit> out;
  for(const auto& group:groups_.at(control))out.push_back(group.Draw({255,255,255,opacity}));
  return out;
}
void BattleSelectionWidgets::CancelPointer(){
  input_.Current=input_.Previous={};
  for(auto& control:groups_)for(auto& group:control){group.Input.TrackedId=-1;group.Input.Flags=0;}
}
}
