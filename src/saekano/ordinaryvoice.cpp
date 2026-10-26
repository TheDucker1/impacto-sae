#include "ordinaryvoice.h"

namespace Impacto::Saekano {
OrdinaryVoiceUpdate UpdateOrdinaryVoice(OrdinaryVoiceGate& state,
                                      uint32_t baseChannel,
                                      OrdinaryVoiceServices& services) {
  const uint32_t channel = baseChannel + uint32_t(state.Slot);
  if (state.Phase == -1) {
    services.BeginReveal(OrdinaryVoiceReveal::Unvoiced);
    state.Phase = -2;
  } else if (state.Phase >= 0) {
    if (services.Ready(channel)) {
      services.Start(channel);
      return {true, -1};
    }
    state.Phase = -3;
  }
  if (state.Phase == -3) {
    if (services.Playing(channel)) {
      services.BeginReveal(OrdinaryVoiceReveal::Voiced);
      state.Phase = -4;
    }
    // Keep both queries in native order: the backend may transition between
    // calls (including during BeginReveal). Ready alone must not fall back.
    if (!services.Playing(channel) && !services.Ready(channel)) {
      services.BeginReveal(OrdinaryVoiceReveal::Fallback);
      state.Phase = -2;
    }
  }
  if (state.Phase == -4 && !services.Playing(channel)) state.Phase = -5;
  OrdinaryVoiceUpdate result;
  if (state.Slot >= 0 && services.Playing(channel))
    result.Position = services.Position(channel);
  return result;
}
}  // namespace Impacto::Saekano
