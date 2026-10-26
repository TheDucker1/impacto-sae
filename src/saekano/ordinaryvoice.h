#pragma once

#include <cstdint>

namespace Impacto::Saekano {
enum class OrdinaryVoiceReveal { Unvoiced, Voiced, Fallback };

// Boundaries of message update84982..84d7e. The owner supplies its actual
// channel allocation; profile voice channel numbers are not assumed here.
class OrdinaryVoiceServices {
 public:
  virtual ~OrdinaryVoiceServices() = default;
  virtual bool Ready(uint32_t channel) = 0;
  virtual bool Playing(uint32_t channel) = 0;
  virtual void Start(uint32_t channel) = 0;
  virtual int32_t Position(uint32_t channel) = 0;
  // Unvoiced: begin next reveal. Voiced: also reset epoch and bind actor lips.
  // Fallback: rebuild character text at200ms, restore color, begin/reset epoch.
  virtual void BeginReveal(OrdinaryVoiceReveal kind) = 0;
};

struct OrdinaryVoiceGate {
  // Native message+70: -1 pending unvoiced, -2 character reveal,
  // nonnegative prepared, -3 waiting, -4 playing, -5 ended.
  int32_t Phase = -1;
  int32_t Slot = -1;  // Native message+74; channel=owner base+slot.
};
struct OrdinaryVoiceUpdate {
  bool SkipTextUpdate = false;
  int32_t Position = -1;  // Native low32 playback position, or unavailable.
};
OrdinaryVoiceUpdate UpdateOrdinaryVoice(OrdinaryVoiceGate& state,
                                      uint32_t baseChannel,
                                      OrdinaryVoiceServices& services);
}  // namespace Impacto::Saekano
