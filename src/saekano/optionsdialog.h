#pragma once
#include "options.h"
#include "savemanager.h"

namespace Impacto::Saekano {
// Shared dialog and native-mode2 storage binding for the Options owner.
// The frontend supplies measured layouts and feeds each visible modal's input.
class OptionsDialogHost {
 public:
  using LayoutFactory=SaveManagerDialogHost::LayoutFactory;
  OptionsDialogHost(const DialogEnvironment& dialog,SaveManagerEnvironment save,
                    LayoutFactory layout,SaveWriteServices& storage);
  DialogState Question;
  DialogInput Input;
  SaveManagerDialogHost Save;
  const std::u16string& Prompt()const{return prompt_;}
  const DialogContentLayout& Layout()const{return layout_;}
  const std::vector<DialogButtonLabels>& Buttons()const{return buttons_;}
  void BeginQuestion(const std::u16string& prompt,uint64_t now);
  OptionsDialogPoll UpdateQuestion(uint64_t now);
  void BeginSave(uint64_t now);
  OptionsSavePoll UpdateSave(uint64_t now);
 private:
  LayoutFactory buildLayout_;
  std::vector<DialogButtonLabels> buttons_;
  std::u16string prompt_;
  DialogContentLayout layout_;
  bool questionOwned_=false;
};
}
