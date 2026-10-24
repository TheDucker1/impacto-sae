#pragma once

#include "profile.h"

namespace Impacto::Saekano {
// Fresh profile sprite's envelope channel, independent of audio position and
// the envelope's text-duration field. Native641e6 and62d4e..62f24.
struct ProfileMouthPlayback {
  bool Active = false, Armed = false;
  uint64_t Epoch = 0;
  std::vector<uint8_t> Samples;
  uint32_t Cycle = 300;  // Fresh sprite constructor628ba.
  int32_t Baseline = 0;
  int32_t From = 2, To = 2, Frame = 2;
  uint64_t CycleStart = 0, CycleEnd = 0;
  void Begin(const ProfileMouthAnimation& envelope, uint64_t now);
  void Update(uint64_t now);
  void Stop();

 private:
  void Constant(int32_t frame);
};
}  // namespace Impacto::Saekano
