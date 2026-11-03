#pragma once
#include "gauge.h"
#include <optional>

namespace Impacto::Saekano {
struct ApproachMissionEnvironment {
  int32_t EntranceAlpha=0, LeftSlide=0, RightSlide=0, Hold=0, Exit=0, ExitDistance=0;
  int32_t LeftX=0, LeftWidth=0, RightX=0;
  size_t PrefixBytes=0;
  static ApproachMissionEnvironment Decode(std::span<const uint8_t> bytes);
};
enum class ApproachMissionAction { PlayVoice, ConfirmSound, DisableVoice };
struct ApproachMissionRequest {
  ApproachMissionAction Kind;
  int32_t Id;
};
struct ApproachMissionState {
  bool Started=false, Visible=false;
  int32_t Mission=-1, Stage=0;
  std::optional<int32_t> PendingStage;
  // Native C14578,145b0,145e8,14620,14658,14690, in order.
  std::array<ScriptLinearTween,6> Tweens;
  std::array<bool,6> Enabled{};
  std::vector<ApproachMissionRequest> Requests;
};
class ApproachMissionAudio {
 public:
  virtual ~ApproachMissionAudio()=default;
  virtual void Register(std::span<const int32_t> ids)=0;
  virtual void Begin(int32_t id,uint64_t now)=0;
  // Result commands prepare during entrance and start only at reveal. Existing
  // mission Begin stays immediate; unsupported adapters must reject the split.
  virtual void Prepare(int32_t);
  virtual void Start(uint64_t);
  virtual void Update(uint64_t now)=0;
  virtual void Disable()=0;
  virtual void Reset()=0;
};
void StartApproachMission(ApproachMissionState& state, const ApproachMissionEnvironment& environment,
                          int32_t mission, uint64_t now);
// Native normal/skip both run this processor. Input is an explicit accepted
// event from the owning host; no automatic confirmation is inferred.
bool UpdateApproachMission(ApproachMissionState& state, const ApproachMissionEnvironment& environment,
                           bool accept, uint64_t now);
}
