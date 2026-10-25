#include "profileitems.h"

#include <bit>
#include <limits>

namespace Impacto::Saekano {
namespace {
class WideTokens {
 public:
  explicit WideTokens(std::span<const uint8_t> bytes) {
    if (bytes.size() % 2 || bytes.size() > 4 * 1024 * 1024)
      throw FormatError("invalid profile item UTF16 extent");
    text_.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size(); i += 2)
      text_.push_back(
          char16_t(uint16_t(bytes[i]) | uint16_t(bytes[i + 1]) << 8));
  }
  bool More() const { return at_ < text_.size(); }
  bool Ended() const { return ended_; }
  void NextRow() { ended_ = false; }
  std::u16string Next() {
    std::u16string result;
    if (ended_) return result;
    while (More()) {
      const auto ch = text_[at_++];
      if (ch == u'"') {
        while (More()) {
          const auto quoted = text_[at_++];
          if (quoted == u'"') {
            if (!More() || text_[at_] != u'"') break;
            ++at_;
          }
          result.push_back(quoted);
        }
      } else if (ch == u'\t' || ch == u'\r' || ch == u'\n') {
        if (ch == u'\r' && More() && text_[at_] == u'\n') {
          ++at_;
          ended_ = true;
        }
        if (!More()) ended_ = true;
        return result;
      } else {
        result.push_back(ch);
      }
    }
    ended_ = true;
    return result;
  }

 private:
  std::u16string text_;
  size_t at_ = 0;
  bool ended_ = false;
};
int32_t Number(std::u16string_view text) {
  size_t i = 0;
  while (i < text.size() &&
         (text[i] == u' ' || text[i] == u'\t' || text[i] == u'\r' ||
          text[i] == u'\n' || text[i] == u'\v' || text[i] == u'\f'))
    ++i;
  const bool negative = i < text.size() && text[i] == u'-';
  if (i < text.size() && (text[i] == u'-' || text[i] == u'+')) ++i;
  const auto first = i;
  uint32_t value = 0;
  while (i < text.size() && text[i] >= u'0' && text[i] <= u'9') {
    const auto digit = uint32_t(text[i++] - u'0');
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10)
      throw FormatError("profile item number exceeds unsigned32 range");
    value = value * 10 + digit;
  }
  if (i == first)
    throw FormatError("profile item number has no decimal digits");
  return std::bit_cast<int32_t>(negative ? 0u - value : value);
}
}  // namespace
std::vector<ProfileItemDefinition> DecodeProfileItemTable(
    std::span<const uint8_t> bytes) {
  WideTokens tokens(bytes);
  std::vector<ProfileItemDefinition> result;
  while (tokens.More()) {
    if (tokens.Next() == u"ON") {
      if (result.size() == 30)
        throw FormatError("profile item table exceeds30 rows");
      ProfileItemDefinition row;
      row.Id = Number(tokens.Next());
      row.Category = Number(tokens.Next());
      row.Name = tokens.Next();
      row.Description = tokens.Next();
      result.push_back(std::move(row));
    } else {
      while (!tokens.Ended()) tokens.Next();
    }
    if (tokens.Ended()) tokens.NextRow();
  }
  return result;
}
std::vector<ProfileItemCharacterTable> LoadProfileItemCatalog(Bytes firstDat) {
  GpdaArchive root(std::move(firstDat));
  if (root.Entries().size() <= 3 || root.Entries()[3].Type != 0)
    throw FormatError(
        "first.dat profile catalog section missing or compressed");
  GpdaArchive tables(root.Data(3));
  if (tables.Entries().size() > 4)
    throw FormatError("profile item catalog exceeds four characters");
  std::vector<ProfileItemCharacterTable> result;
  for (size_t i = 0; i < tables.Entries().size(); ++i) {
    if (tables.Entries()[i].Type != 0)
      throw FormatError("profile item table is compressed");
    result.push_back(
        {tables.Entries()[i].Name, DecodeProfileItemTable(tables.Data(i))});
  }
  return result;
}
}  // namespace Impacto::Saekano
