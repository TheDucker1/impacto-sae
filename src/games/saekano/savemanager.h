#pragma once
#include "endingdialog.h"
#include <array>

namespace Impacto::Saekano {
struct SaveManagerEnvironment {
  std::array<uint8_t,3> Flags{};
  std::array<std::u16string,56> Strings;
  static SaveManagerEnvironment Decode(std::span<const uint8_t> bytes);
  const std::u16string& Mode2Message(int32_t nativeCase)const;
};
enum class SaveWriteStage { System, Slots };
struct SaveWriteStatus { bool Done=false;uint32_t Error=0; };
struct SaveWorkerServices {
  virtual ~SaveWorkerServices()=default;
  virtual bool DialogReady()const=0;
  virtual bool DialogVisible()const=0;
  virtual void ShowMessage(int32_t nativeCase,uint64_t now)=0;
  virtual void CloseMessage()=0;
  virtual void UpdateMessage(uint64_t now)=0;
  virtual void Snapshot(SaveWriteStage stage)=0;
  virtual void BeginWrite(SaveWriteStage stage)=0;
  virtual SaveWriteStatus PollWrite()=0;
};
struct SaveWorkerState {
  bool Started=false,Active=false,Accepted=false;
  int32_t Phase=0;
  std::optional<int32_t> PendingPhase;
};
// Mode2 path after storage initialization. Vita platform repair dialogs are
// separate from this path; special native errors are explicitly rejected.
void BeginMode2Save(SaveWorkerState& state);
void UpdateMode2Save(SaveWorkerState& state,SaveWorkerServices& services,uint64_t now);
struct SaveWriteServices {
  virtual ~SaveWriteServices()=default;
  virtual void Snapshot(SaveWriteStage stage)=0;
  virtual void BeginWrite(SaveWriteStage stage)=0;
  virtual SaveWriteStatus PollWrite()=0;
};
// Real shared modal lifecycle; the application supplies its measured text
// layouts and storage implementation. Group0 is the native single-button group.
class SaveManagerDialogHost final:public EndingSaveServices,private SaveWorkerServices {
 public:
  using LayoutFactory=std::function<DialogContentLayout(const std::u16string&,size_t)>;
  SaveManagerDialogHost(SaveManagerEnvironment environment,LayoutFactory layout,SaveWriteServices& storage);
  DialogState Modal;
  DialogInput Input;
  const SaveWorkerState& State()const{return state_;}
  const std::u16string& Prompt()const{return prompt_;}
  const DialogContentLayout& Layout()const{return layout_;}
  void Begin(int32_t nativeMode,uint64_t now)override;
  EndingSaveStatus Update(uint64_t now)override;
 private:
  SaveManagerEnvironment environment_;
  LayoutFactory buildLayout_;
  SaveWriteServices& storage_;
  SaveWorkerState state_;
  std::u16string prompt_;
  DialogContentLayout layout_;
  bool DialogReady()const override{return Modal.Phase==3;}
  bool DialogVisible()const override{return Modal.Visible;}
  void ShowMessage(int32_t nativeCase,uint64_t now)override;
  void CloseMessage()override{CloseDialog(Modal);}
  void UpdateMessage(uint64_t now)override;
  void Snapshot(SaveWriteStage stage)override{storage_.Snapshot(stage);}
  void BeginWrite(SaveWriteStage stage)override{storage_.BeginWrite(stage);}
  SaveWriteStatus PollWrite()override{return storage_.PollWrite();}
};
}
