#include "titleinput.h"
#include "commands.h"

namespace Impacto::Saekano {
TitleWidgetInput::TitleWidgetInput(const TitleEnvironment& e,uint32_t holdFrames){
  const auto& menu=e.RectangleTables.at(0x358);
  if(menu.size()<4)throw FormatError("title menu has fewer than four contact regions");
  for(size_t i=0;i<4;++i)rectangles_[i]=menu[i];
  rectangles_[4]=e.Rectangles.at(0x368);
  components_.reserve(5);for(size_t i=0;i<5;++i)components_.emplace_back(holdFrames);
}
void TitleWidgetInput::BeginFrame(WidgetInputHistory::Snapshot snapshot){input_.BeginFrame(std::move(snapshot));}
void TitleWidgetInput::UpdateComponent(size_t index){
  auto& c=components_.at(index);
  c.Update(SampleWidgetContact(rectangles_.at(index),input_.Current[0],input_.Previous[0],c.TrackedId,input_.Velocity(0)));
  input_.Publish(c.Flags);
}
bool TitleWidgetInput::UpdatePressStart(){UpdateComponent(4);return components_[4].Enabled && (components_[4].Flags&8)!=0;}
std::optional<size_t> TitleWidgetInput::UpdateMenu(){
  std::optional<size_t> released;
  for(size_t i=0;i<4;++i){UpdateComponent(i);if(!released && components_[i].Enabled && (components_[i].Flags&8))released=i;}
  return released;
}
void TitleWidgetInput::CancelPointer(){
  input_.Current=input_.Previous={};
  for(auto& c:components_){c.TrackedId=-1;c.Flags=0;}
}
}
