#pragma once
#include "optionspages.h"
#include "battlewidgets.h"
namespace Impacto::Saekano {
struct OptionsWidgetComponent {
  bool Rectangle=true;
  // The shared native input base and static sprite path are already ported here.
  BattleCardSprite Body;
  OptionsWidgetComponent(bool rectangle,UiSpriteDefinition shape,ProfileTextureKey texture,
                         bool interactive,uint32_t hold):Rectangle(rectangle),Body(shape,texture,1,interactive,hold){}
};
struct OptionsWidgetGroup {std::vector<OptionsWidgetComponent> Variants;size_t Selected=0;};
struct OptionsWidgetRecord {OptionsWidgetId Id;std::vector<OptionsWidgetGroup> Groups;};
// Actual53302 widget construction. Collection updates every variant only when
// that widget's owner delivers Update; queries/drawing use its selected variant.
class OptionsWidgets {
 public:
  OptionsWidgets(const OptionsEnvironment& environment,const OptionsValues& values,
                 bool alternateHeader,uint32_t holdFrames);
  void BeginFrame(WidgetInputHistory::Snapshot contacts){input_.BeginFrame(std::move(contacts));}
  void Update(OptionsWidgetId id,int32_t x,int32_t y,uint64_t now);
  OptionsWidgetContact Contact(OptionsWidgetId id)const;
  void SetToggleVariant(uint32_t page,uint32_t index,bool inverted);
  std::vector<UiRenderCommand> Draw(OptionsWidgetId id,std::array<uint8_t,4> tint={255,255,255,255})const;
  void CancelPointer();
  const std::vector<OptionsWidgetRecord>& Records()const{return records_;}
  const WidgetInputHistory& Input()const{return input_;}
 private:
  WidgetInputHistory input_;
  std::vector<OptionsWidgetRecord> records_;
  OptionsWidgetRecord& Find(OptionsWidgetId id);
  const OptionsWidgetRecord& Find(OptionsWidgetId id)const;
};
}
