#include "ordinaryvoice.h"
#include "formats.h"
#include "resources.h"
#include <algorithm>
#include <bit>
#include <string>

namespace Impacto::Saekano {
namespace {
// Native narrow tokenizer1815d8, also used by profile.cpp. CRLF ends a row;
// bare CR/LF delimit fields. Quotes/doubled quotes match the existing parser.
class VoiceTokens {
 public:
  explicit VoiceTokens(std::span<const uint8_t> text) : text_(text) {}
  bool Done() const { return position_ == text_.size(); }
  void BeginRow() { ended_ = Done(); }
  std::string Next() {
    std::string result;
    if (ended_) return result;
    while (!Done()) {
      const auto c = text_[position_++];
      if (c == '"') {
        while (!Done()) {
          const auto q = text_[position_++];
          if (q != '"') result.push_back(char(q));
          else if (!Done() && text_[position_] == '"') {
            ++position_;
            result.push_back('"');
          } else break;
        }
      } else if (c == ',' || c == '\r' || c == '\n') {
        if (c == '\r' && !Done() && text_[position_] == '\n') {
          ++position_;
          ended_ = true;
        }
        ended_ = ended_ || Done();
        return result;
      } else result.push_back(char(c));
    }
    ended_ = true;
    return result;
  }
  void FinishRow() { while (!ended_) Next(); }
 private:
  std::span<const uint8_t> text_;
  size_t position_ = 0;
  bool ended_ = false;
};
int32_t VoiceNumber(std::string_view text) {
  uint32_t number = 0;
  bool negative = false;
  for (const auto c : text) {
    if (c >= '0' && c <= '9') number = number * 10u + uint32_t(c - '0');
    if (c == '-') negative = true;
  }
  return std::bit_cast<int32_t>(negative ? 0u - number : number);
}
}  // namespace
OrdinaryVoiceSettings MakeFreshOrdinaryVoiceSettings() {
  return {10, false, {1, 1, 1, 1, 1, 1, 1, 1, 1}};
}
std::vector<OrdinaryVoiceRange> DecodeOrdinaryVoiceRanges(
    std::span<const uint8_t> text) {
  if (text.size() > 32 * 1024 * 1024)
    throw FormatError("voice range text exceeds budget");
  VoiceTokens tokens(text);
  tokens.Next();
  const auto count = VoiceNumber(tokens.Next());
  if (count < 0 || count > 1000000)
    throw FormatError("voice range count exceeds budget");
  std::vector<OrdinaryVoiceRange> result(size_t(count), {0, 0, 0});
  tokens.FinishRow();
  size_t row = 0;
  while (!tokens.Done()) {
    tokens.BeginRow();
    if (tokens.Next() != "ON") { tokens.FinishRow(); continue; }
    if (row == result.size()) throw FormatError("voice rows exceed declared count");
    const auto group = VoiceNumber(tokens.Next());
    const auto first = VoiceNumber(tokens.Next());
    const auto last = VoiceNumber(tokens.Next());
    if (group < 0 || group > 8) throw FormatError("voice group exceeds settings");
    result[row++] = {group, first, first == 65535 ? 65535 : last};
    tokens.FinishRow();
  }
  return result;
}
std::vector<OrdinaryVoiceRange> LoadOrdinaryVoiceRangesFromFirst(Bytes firstDat) {
  GpdaArchive first(std::move(firstDat));
  GpdaArchive text(first.Data(8));
  return DecodeOrdinaryVoiceRanges(InflateGzip(text.Data(2)));
}
OrdinaryVoiceEligibility OrdinaryVoiceEligible(
    int32_t voiceId, std::span<const OrdinaryVoiceRange> ranges,
    const OrdinaryVoiceSettings& settings) {
  if (voiceId < 0) throw FormatError("voice eligibility requires nonnegative ID");
  OrdinaryVoiceEligibility result{true, -1};
  for (const auto& range : ranges) {
    if (range.Group < 0 || range.Group > 8)
      throw FormatError("voice group exceeds settings");
    if (voiceId >= range.First && voiceId <= range.Last) {
      result.Group = range.Group;
      if (!settings.Enabled[size_t(range.Group)]) result.Enabled = false;
    }
  }
  if (result.Group == -1) {
    result.Group = 8;
    if (!settings.Enabled[8]) result.Enabled = false;
  }
  return result;
}
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
