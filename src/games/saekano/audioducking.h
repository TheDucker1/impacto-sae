#pragma once
#include <cstdint>
#include <optional>
namespace Impacto::Saekano {
// Game startup registers target0 and trigger voices7..12. Device states remain
// inputs; prepared sources do not count as active playback.
struct AudioDuckingInputs {
  uint32_t Busy=0,Prepared=0,Available=0x1f80;
  bool TargetAvailable=true;
};
struct BgmVoiceAttenuation {
  bool Enabled=false,Started=false;
  int32_t Percent=0,SavedGain=0,CurrentGain=100;
  uint64_t Epoch=0;
  int32_t Phase=0,Delta=0,Base=0;
  void SetEnabled(bool enabled){Enabled=enabled;}
  void SetPercent(int32_t percent);
  // Native b6540 with update-saved flag1; percentage is already converted by
  // the game audio service. Automatic publications leave SavedGain intact.
  void SetGain(int32_t percent,bool targetAvailable=true);
  void StartVoice(uint32_t channel,bool ready,uint64_t now,bool targetAvailable=true);
  std::optional<int32_t> Tick(uint64_t now,AudioDuckingInputs input);
};
}
