#pragma once
#include "title.h"
#include "widgetinput.h"

namespace Impacto::Saekano {
// Four unconditional menu groups followed by the Press Start group. All use
// the shared one-contact rectangle component; transparent faces are irrelevant
// to contact collection. BeginFrame once per platform frame, then update only
// the branch called by the title router (phase5 or phase10's pointer branch).
class TitleWidgetInput {
 public:
  TitleWidgetInput(const TitleEnvironment& environment,uint32_t holdFrames);
  void BeginFrame(WidgetInputHistory::Snapshot snapshot);
  bool UpdatePressStart();
  std::optional<size_t> UpdateMenu();
  void CancelPointer();
  const WidgetInputHistory& Input()const{return input_;}
  const std::vector<WidgetEventHistory>& Components()const{return components_;}
 private:
  void UpdateComponent(size_t index);
  std::array<std::array<int32_t,4>,5> rectangles_;
  WidgetInputHistory input_;
  std::vector<WidgetEventHistory> components_;
};
}
