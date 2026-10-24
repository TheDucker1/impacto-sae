#include "profile.h"

#include <bit>
#include <limits>

namespace Impacto::Saekano {
namespace {
// Native narrow tokenizer801815d8. Only CRLF ends a row; bare CR/LF delimit
// fields. The profile caller resets row-end only after rejecting a non-ON
// token.
class Tokens {
 public:
  explicit Tokens(std::span<const uint8_t> text) : text_(text) {}
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
          if (q != '"')
            result.push_back(char(q));
          else if (!Done() && text_[position_] == '"') {
            ++position_;
            result.push_back('"');
          } else
            break;
        }
      } else if (c == ',' || c == '\r' || c == '\n') {
        if (c == '\r' && !Done() && text_[position_] == '\n') {
          ++position_;
          ended_ = true;
        }
        ended_ = ended_ || Done();
        return result;
      } else
        result.push_back(char(c));
    }
    ended_ = true;
    return result;
  }
  void FinishRow() {
    while (!ended_) Next();
  }

 private:
  std::span<const uint8_t> text_;
  size_t position_ = 0;
  bool ended_ = false;
};
int32_t Number(std::string_view text) {
  // Imported _Stoul(base10), unsigned32 then native signed32 field store.
  size_t i = 0;
  while (i < text.size() &&
         (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
          text[i] == '\n' || text[i] == '\v' || text[i] == '\f'))
    ++i;
  bool negative = i < text.size() && text[i] == '-';
  if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
  const auto first = i;
  uint32_t value = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    const auto digit = uint32_t(text[i++] - '0');
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10)
      throw FormatError("profile numeric value exceeds unsigned32 range");
    value = value * 10 + digit;
  }
  if (i == first)
    throw FormatError("profile numeric token contains no decimal digits");
  return std::bit_cast<int32_t>(negative ? 0u - value : value);
}
std::string DualText(std::string text) {
  // The native dual-string constructor receives a C string, not its CSV length.
  if (const auto end = text.find('\0'); end != std::string::npos)
    text.resize(end);
  return text;
}
}  // namespace
ProfileScene DecodeProfileScene(std::span<const uint8_t> bytes) {
  if (bytes.size() > 32 * 1024 * 1024)
    throw FormatError("profile scene exceeds input budget");
  Tokens tokens(bytes);
  ProfileScene result;
  while (!tokens.Done()) {
    if (tokens.Next() != "ON") {
      tokens.FinishRow();
      tokens.BeginRow();
      continue;
    }
    const auto kind = tokens.Next();
    if (kind == "CHARA") {
      if (result.size() >= 1000000)
        throw FormatError("profile character count exceeds host budget");
      result.push_back({Number(tokens.Next()), {}});
    } else if (kind == "DATA") {
      if (result.empty()) throw FormatError("profile DATA precedes CHARA");
      auto& events = result.back().Events;
      if (events.size() == 10)
        throw FormatError("profile rows exceed native ten-entry capacity");
      ProfileEvent event;
      for (auto& value : event.Values) value = Number(tokens.Next());
      event.ScriptName = DualText(tokens.Next());
      for (auto& value : event.Presentation) value = Number(tokens.Next());
      event.Response = DualText(tokens.Next());
      event.Tail = Number(tokens.Next());
      events.push_back(std::move(event));
    }
  }
  return result;
}
}  // namespace Impacto::Saekano
