#include "ordinaryvoice.h"
#include "formats.h"
#include <algorithm>

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
OrdinaryVoiceQueue MakeFreshOrdinaryVoiceQueue() { return {{}, 7, 0, {}}; }
uint32_t PrepareOrdinaryVoice(OrdinaryVoiceQueue& state, int16_t voiceId,
                             bool lookahead,
                             OrdinaryVoicePreparation& services) {
  const auto index = [&](int16_t id) {
    const auto found = std::find(state.Catalog.begin(), state.Catalog.end(), id);
    if (id < 0 || found == state.Catalog.end())
      throw FormatError("ordinary voice is absent from registered catalog");
    return size_t(found - state.Catalog.begin());
  };
  index(voiceId);
  if (state.NextSlot > 1)
    throw FormatError("invalid ordinary voice next slot");
  for (const auto& prepared : state.Prepared) {
    index(prepared.VoiceId);
    if (prepared.Slot > 1)
      throw FormatError("invalid prepared ordinary voice slot");
  }
  uint32_t slot;
  if (!state.Prepared.empty() && state.Prepared.front().VoiceId == voiceId) {
    slot = state.Prepared.front().Slot;
    services.Start(state.BaseChannel + slot);
    state.Prepared.erase(state.Prepared.begin());
  } else {
    state.Prepared.clear();
    services.Stop(state.BaseChannel);
    services.Stop(state.BaseChannel + 1);
    slot = state.NextSlot;
    services.Prepare(state.BaseChannel + slot, voiceId);
    services.Start(state.BaseChannel + slot);
    state.NextSlot = (state.NextSlot + 1) & 1;
  }
  if (lookahead) {
    const auto previous =
        state.Prepared.empty() ? voiceId : state.Prepared.back().VoiceId;
    const size_t next = index(previous) + 1;
    if (state.Prepared.empty() && next < state.Catalog.size()) {
      services.Prepare(state.BaseChannel + state.NextSlot, state.Catalog[next]);
      state.Prepared.push_back({state.NextSlot, state.Catalog[next]});
      state.NextSlot = (state.NextSlot + 1) & 1;
    }
  }
  return slot;
}
}  // namespace Impacto::Saekano
