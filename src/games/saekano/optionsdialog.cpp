#include "optionsdialog.h"
#include "commands.h"

namespace Impacto::Saekano {
OptionsDialogHost::OptionsDialogHost(const DialogEnvironment& dialog,
    SaveManagerEnvironment save,LayoutFactory layout,SaveWriteServices& storage)
    :Save(std::move(save),layout,storage),buildLayout_(std::move(layout)){
  if(dialog.ButtonGroups.size()<=2 || dialog.ButtonGroups[2].empty() ||
      dialog.ButtonGroups[2].size()>3)
    throw FormatError("Options save-question button group is unavailable");
  buttons_=dialog.ButtonGroups[2];
}
void OptionsDialogHost::BeginQuestion(const std::u16string& prompt,uint64_t now){
  if(Question.Visible || Save.State().Active)
    throw FormatError("Options dialog is already active");
  if(prompt.empty())throw FormatError("Options save question is unavailable");
  prompt_=prompt;layout_=buildLayout_(prompt_,buttons_.size());
  // 57372 calls 8a8c(dialog,2,1,0,1): group2, cancel, initial0, close.
  BeginDialog(Question,layout_,buttons_.size(),0,true,true,now);
  Question.NativeByte292=1;
  Input={};questionOwned_=true;
}
OptionsDialogPoll OptionsDialogHost::UpdateQuestion(uint64_t now){
  if(!questionOwned_)throw UnsupportedCommand("Options question Begin was suppressed");
  UpdateDialog(Question,Input,now);Input={};
  return {Question.Visible,Question.Canceled,Question.Result};
}
void OptionsDialogHost::BeginSave(uint64_t now){
  if(Question.Visible)throw FormatError("Options save question is still visible");
  Save.Begin(2,now);Input={};questionOwned_=false;
}
OptionsSavePoll OptionsDialogHost::UpdateSave(uint64_t now){
  const auto result=Save.Update(now);
  return {result.Active,uint8_t(result.Accepted)};
}
}
