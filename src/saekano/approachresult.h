#pragma once
#include "gauge.h"
#include <optional>

namespace Impacto::Saekano {
struct ApproachResultCommand {
  struct Outcome { int32_t Destination=0, Voice=-1; std::u16string Text; };
  int32_t RequiredGauge=0;
  std::array<Outcome,2> Outcomes;
  static ApproachResultCommand Decode(std::span<const uint8_t> payload);
};
struct ApproachResultEnvironment {
  ModeResultEnvironment Animation;
  int32_t FadeDuration=0;
  static ApproachResultEnvironment Decode(std::span<const uint8_t> bytes);
};
enum class ApproachResultAction {
  PrepareText, DisableVoice, PrepareVoice, BeginReveal, StartVoice,
  RevealText, Sound, Trophy, Complete
};
struct ApproachResultRequest { ApproachResultAction Kind; int32_t Id; };
struct ApproachResultInput {
  // The host updates the real text service before this phase3 input is sampled.
  // A reveal request takes effect on the following update, as in the native path.
  bool TextRevealed=false, Accept=false, Released=false;
};
struct ApproachResultState {
  bool Started=false, Visible=false, Cursor=false, Completed=false;
  int32_t Phase=0;
  std::optional<int32_t> PendingPhase;
  size_t Outcome=0;
  ApproachResultCommand::Outcome Selected;
  // Native C146c8,14700,14738,14770,147a8,147e0,14a08.
  std::array<ScriptLinearTween,7> Tweens;
  std::array<bool,7> Enabled{};
  ScriptLinearTween Fade;
  bool FadeActive=false;
  int32_t FadeOpacity=0;
  std::vector<ApproachResultRequest> Requests;
};
void StartApproachResult(ApproachResultState& state,
                         const ApproachResultCommand& command,
                         const ApproachResultEnvironment& environment,
                         int32_t gauge,uint64_t now);
// Normal and skip wrappers share this processor; skip does not imply Accept.
// The owner must consume presentation/audio requests and apply the existing
// semantic completion exactly once after Complete. This alone is not playback.
bool UpdateApproachResult(ApproachResultState& state,
                          const ApproachResultEnvironment& environment,
                          const ApproachResultInput& input,uint64_t now);
}
