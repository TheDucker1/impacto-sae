#pragma once

#include <cstdint>
#include <vector>

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

struct PreparedOrdinaryVoice {
  uint32_t Slot;
  int16_t VoiceId;
};
struct OrdinaryVoiceQueue {
  std::vector<int16_t> Catalog;  // Unique IDs in native registration order.
  uint32_t BaseChannel;
  uint32_t NextSlot;
  std::vector<PreparedOrdinaryVoice> Prepared;
};
// Explicit context construction1a053a..05ae only; never use for a restore.
OrdinaryVoiceQueue MakeFreshOrdinaryVoiceQueue();
class OrdinaryVoicePreparation {
 public:
  virtual ~OrdinaryVoicePreparation() = default;
  virtual void Stop(uint32_t channel) = 0;
  virtual void Prepare(uint32_t channel, int16_t voiceId) = 0;
  virtual void Start(uint32_t channel) = 0;
};
// Native1ad708: consume a matching prepared entry or stop/reprepare both slots,
// start requested voice, and optionally prepare one next catalog entry.
uint32_t PrepareOrdinaryVoice(OrdinaryVoiceQueue& state, int16_t voiceId,
                             bool lookahead,
                             OrdinaryVoicePreparation& services);
}  // namespace Impacto::Saekano
