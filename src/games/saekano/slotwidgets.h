#pragma once
#include "slotenv.h"
#include "battlewidgets.h"
#include <functional>
namespace Impacto::Saekano {
struct SlotWidgetComponent {
  bool Rectangle=true;
  BattleCardSprite Body;
  SlotWidgetComponent(bool rectangle,UiSpriteDefinition shape,ProfileTextureKey texture,bool interactive,uint32_t hold)
      :Rectangle(rectangle),Body(shape,texture,1,interactive,hold){}
};
struct SlotWidgetRecord {
  SlotBrowserWidget Kind=SlotBrowserWidget::Page;
  uint32_t Row=0;
  std::vector<SlotWidgetComponent> Components; // Every native group has one variant.
};
// Native5bd3a: four activation bodies, four protection bodies, flick area and
// Back's visible sprite plus separate contact rectangle.
class SlotWidgets {
 public:
  SlotWidgets(const SlotEnvironment& environment,uint32_t holdFrames);
  void BeginFrame(WidgetInputHistory::Snapshot snapshot){input_.BeginFrame(std::move(snapshot));}
  void Update(SlotBrowserWidget kind,uint32_t row,uint64_t now);
  bool Released(SlotBrowserWidget kind,uint32_t row)const;
  bool PageFlick()const; // Native9c556 -> component0x80, never a hold test.
  float PageVelocityY()const; // Native9c936 -> released vertical velocity.
  std::vector<UiRenderCommand> Draw(SlotBrowserWidget kind,uint32_t row,std::array<uint8_t,4> tint={255,255,255,255})const;
  void CancelPointer();
  const std::vector<SlotWidgetRecord>& Records()const{return records_;}
  const WidgetInputHistory& Input()const{return input_;}
 private:
  WidgetInputHistory input_;
  std::vector<SlotWidgetRecord> records_;
  SlotWidgetRecord& Find(SlotBrowserWidget kind,uint32_t row);
  const SlotWidgetRecord& Find(SlotBrowserWidget kind,uint32_t row)const;
};
// UI-host bridge: shift contacts once per frame, then let the browser's native
// arbitration decide exactly which components collect/query. Own no audio state.
class SlotWidgetServices final:public SlotBrowserServices {
 public:
  SlotWidgetServices(SlotWidgets& widgets,std::function<void(int32_t)> sound);
  void BeginFrame(WidgetInputHistory::Snapshot snapshot,uint64_t now);
  void UpdateWidget(SlotBrowserWidget kind,uint32_t row)override;
  bool PageFlick()override;
  float PageVelocityY()override;
  bool Released(SlotBrowserWidget kind,uint32_t row)override;
  void UiSound(int32_t id)override;
 private:
  SlotWidgets& widgets_;
  std::function<void(int32_t)> sound_;
  uint64_t now_=0;
};
}
