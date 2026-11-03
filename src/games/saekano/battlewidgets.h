#pragma once
#include "gauge.h"
#include "widgetinput.h"

namespace Impacto::Saekano {
// Concrete sprite component99d74/9a122/9a1e8, restricted to the native battle
// card's static and pulsing scale modes. Both widget groups are visible layers.
struct BattleCardSprite {
  UiSpriteDefinition Shape;
  ProfileTextureKey Texture;
  float Scale=1;
  std::array<int32_t,2> Offset{};
  WidgetEventHistory Input;
  ScriptLinearTween Pulse,Auxiliary;
  bool Pulsing=false,AuxiliaryEnabled=false;
  BattleCardSprite(UiSpriteDefinition shape,ProfileTextureKey texture,float scale,
      bool interactive,uint32_t holdFrames);
  void ConfigurePulse(float from,float to,int32_t duration,uint64_t now);
  void Update(std::array<int32_t,2> offset,const WidgetContactEvents& contact,uint64_t now);
  std::array<int32_t,4> HitRectangle(std::array<int32_t,2> offset)const;
  bool Pressed()const { return Input.Enabled && (Input.Flags&1)!=0; }
  UiSpriteBlit Draw(std::array<uint8_t,4> color)const;
};
class BattleSelectionWidgets {
 public:
  BattleSelectionWidgets(UiSpriteDefinition shape,uint32_t holdFrames):shape_(shape),holdFrames_(holdFrames){}
  void BeginFrame(WidgetInputHistory::Snapshot contacts){input_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  void Clear(size_t control){groups_.at(control).clear();}
  void Add(size_t control,uint32_t texture,std::array<int32_t,2> position,float scale,
      std::array<uint8_t,4> color,bool interactive);
  void ConfigurePulse(size_t control,size_t group,float from,float to,int32_t duration,uint64_t now);
  // Called by the UI's actual offset deliveries, not by every render frame.
  void SetOffset(size_t control,int32_t y,uint64_t now);
  bool Pressed(size_t control)const;
  std::vector<UiSpriteBlit> Draw(size_t control,uint8_t opacity)const;
  const std::vector<BattleCardSprite>& Groups(size_t control)const{return groups_.at(control);}
 private:
  UiSpriteDefinition shape_;
  uint32_t holdFrames_;
  WidgetInputHistory input_;
  std::array<std::vector<BattleCardSprite>,3> groups_;
};
}
