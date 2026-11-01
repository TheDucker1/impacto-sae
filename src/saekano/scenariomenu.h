#pragma once

namespace Impacto::Saekano {
// Manager74a0a..74a52: publication of M14fc4 follows the revealed cursor
// flag M1668 and input2's hide-text toggle, rather than a generic VM wait.
bool ScenarioMenuInputReady(bool revealPublished,bool input2Pressed,bool battleMode);
struct ScenarioMenuEntryInput {
  bool EndRequested=false,BattleMode=false,ApproachMode=false;
  bool InputReady=false;
  // M1451c=C14514 resets to0; its wider writers remain unresolved. Do not replace it
  // with the unrelated approach-gauge-changing byte C1451c.
  bool Native1451c=false;
  bool Input3Pressed=false,WidgetReleased=false;
};
// Wrapper3024e..3029c, after the owning manager's update. Input polling and
// widget sampling remain real host services; this predicate advances nothing.
bool ShouldEnterScenarioMenu(const ScenarioMenuEntryInput& input);
}
