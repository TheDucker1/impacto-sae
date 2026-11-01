#include "scenariomenu.h"

namespace Impacto::Saekano {
bool ScenarioMenuInputReady(bool revealed,bool toggle,bool battle) {
  return revealed && (!toggle || battle);
}
bool ShouldEnterScenarioMenu(const ScenarioMenuEntryInput& i) {
  return !i.EndRequested && !i.BattleMode && !i.ApproachMode && i.InputReady &&
         !i.Native1451c && (i.Input3Pressed || i.WidgetReleased);
}
}
