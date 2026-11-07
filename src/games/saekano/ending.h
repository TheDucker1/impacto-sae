#pragma once
#include "gauge.h"
#include <optional>

namespace Impacto::Saekano {
struct EndingEnvironment {
  std::u16string ClearSavePrompt,DiscardClearPrompt,SystemSavePrompt;
  size_t PrefixBytes=0;
  uint32_t ParticleOpacityGroups=0; // Native script environment802607cc.
  static EndingEnvironment Decode(std::span<const uint8_t> bytes);
};
struct EndingDialogStatus { bool Visible=true; int32_t Result=-1; };
struct EndingSaveStatus { bool Active=true, Accepted=false; };
// These services own real modal UI/save operations. Returning a title request
// is distinct from constructing the title or committing persistent save data.
struct ScenarioEndingServices {
  virtual ~ScenarioEndingServices()=default;
  virtual void OpenPrompt(uint64_t now)=0;
  virtual EndingDialogStatus UpdatePrompt(uint64_t now)=0;
  virtual void RetirePrompt()=0;
  virtual void BeginEndingSave(uint64_t now)=0;
  virtual EndingSaveStatus UpdateEndingSave(uint64_t now)=0;
  virtual void RequestTitle()=0;
};
struct ScenarioEndingState {
  bool Started=false, TitleRequested=false;
  int32_t Phase=22;
  std::optional<int32_t> PendingPhase;
  ScriptLinearTween Fade;
};
// Normal manager exhaustion enters original wrapper phase22. A frontend must
// retain the manager until the owning scene transition retires it.
void StartScenarioEnding(ScenarioEndingState& state);
// Executes wrapper22..26 in original order, including same-frame immediate
// requests and deferred phase26. The optional phase25 duration is an authored
// environment input; normal decline/save-complete paths use constant 1ms.
void UpdateScenarioEnding(ScenarioEndingState& state,ScenarioEndingServices& host,
                          uint64_t now,std::optional<int32_t> fadeDuration={});
}
