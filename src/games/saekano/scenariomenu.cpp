#include "scenariomenu.h"
#include "formats.h"

namespace Impacto::Saekano {
bool ScenarioMenuInputReady(bool revealed,bool toggle,bool battle) {
  return revealed && (!toggle || battle);
}
bool ShouldEnterScenarioMenu(const ScenarioMenuEntryInput& i) {
  return !i.EndRequested && !i.BattleMode && !i.ApproachMode && i.InputReady &&
         !i.Native1451c && (i.Input3Pressed || i.WidgetReleased);
}
int32_t UpdateSystemMenuInput(int32_t& selected,const SystemMenuInput& in,SystemMenuInputServices& h) {
  if(selected<0 || selected>=5)throw FormatError("system menu selection outside five rows");
  int32_t result=0;bool changed=false;
  if(in.Previous || in.Next) {
    h.UiSound(2);h.Highlight(selected,false);
    selected=(selected+(in.Previous?4:1))%5;changed=true;
  }else if(in.Accept) {
    h.UiSound(3);result=selected+1;
  }else {
    for(size_t i=0;i<in.RowReleased.size();++i)if(in.RowReleased[i]) {
      h.UiSound(3);h.Highlight(selected,false);selected=int32_t(i);
      result=selected+1;changed=true;
    }
  }
  if(changed){h.RestartSelectionEffects(selected);h.Highlight(selected,true);}
  if(in.Cancel || in.BackReleased || in.ExitWidgetActivated){h.UiSound(4);result=-1;}
  return result;
}
}
