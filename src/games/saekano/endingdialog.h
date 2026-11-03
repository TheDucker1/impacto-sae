#pragma once
#include "ending.h"
#include "dialog.h"
#include <functional>

namespace Impacto::Saekano {
struct EndingSaveServices {
  virtual ~EndingSaveServices()=default;
  virtual void Begin(int32_t nativeMode,uint64_t now)=0;
  virtual EndingSaveStatus Update(uint64_t now)=0;
};
// Shared modal implementation; the application supplies real save and scene
// services. This owns the original group1/initial0/no-cancel dialog lifecycle.
class ScenarioEndingDialogHost final:public ScenarioEndingServices {
 public:
  ScenarioEndingDialogHost(std::u16string prompt,DialogContentLayout layout,size_t buttons,
                           EndingSaveServices& saves,std::function<void()> requestTitle);
  DialogState Modal;
  DialogInput Input;
  const std::u16string& Prompt()const{return prompt_;}
  void OpenPrompt(uint64_t now)override;
  EndingDialogStatus UpdatePrompt(uint64_t now)override;
  void RetirePrompt()override;
  void BeginEndingSave(uint64_t now)override;
  EndingSaveStatus UpdateEndingSave(uint64_t now)override;
  void RequestTitle()override;
 private:
  std::u16string prompt_;
  DialogContentLayout layout_;
  size_t buttons_;
  EndingSaveServices& saves_;
  std::function<void()> requestTitle_;
  bool owned_=false;
};
}
