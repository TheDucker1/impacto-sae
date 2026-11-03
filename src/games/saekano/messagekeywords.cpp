#include "messagekeywords.h"

#include "commands.h"

#include <optional>
#include <string_view>

namespace Impacto::Saekano {
namespace {
bool Space(char16_t ch) {
  return ch == u' ' || ch == u'\t' || ch == u'\r' || ch == u'\n';
}
}  // namespace
std::vector<int32_t> OriginalMessageKeywords(const std::u16string& text) {
  std::vector<int32_t> result;
  std::optional<int32_t> open;
  size_t cursor = 0;
  while ((cursor = text.find(u'\u300a', cursor)) != std::u16string::npos) {
    const auto end = text.find(u'\u300b', cursor + 1);
    if (end == std::u16string::npos)
      throw FormatError("unterminated message control tag");
    auto tag = text.substr(cursor + 1, end - cursor - 1);
    cursor = end + 1;
    // Native80016d16 folds ASCII letters inside control tags to upper case.
    for (auto& ch : tag)
      if (ch >= u'a' && ch <= u'z') ch -= u'a' - u'A';
    std::vector<std::u16string_view> tokens;
    size_t begin = 0;
    while (begin < tag.size()) {
      while (begin < tag.size() && Space(tag[begin])) ++begin;
      auto finish = begin;
      while (finish < tag.size() && !Space(tag[finish])) ++finish;
      if (finish > begin)
        tokens.emplace_back(tag.data() + begin, finish - begin);
      begin = finish;
    }
    if (tokens.empty()) continue;
    if (tokens[0] == u"KEYWORD") {
      // The original corpus uses KEYWORD = decimal; reject malformed or
      // unverified variants instead of writing outside the native flag bank.
      if (open || tokens.size() != 3 || tokens[1] != u"=" || tokens[2].empty())
        throw FormatError("unsupported message keyword tag syntax");
      int32_t value = 0;
      for (auto ch : tokens[2]) {
        if (ch < u'0' || ch > u'9' || value > 1023 / 10)
          throw FormatError("message keyword index outside bank0");
        value = value * 10 + ch - u'0';
      }
      if (value >= 1024)
        throw FormatError("message keyword index outside bank0");
      open = value;
    } else if (tokens[0] == u"/KEYWORD") {
      if (!open || tokens.size() != 1)
        throw FormatError("unmatched message keyword closing tag");
      result.push_back(*open);
      open.reset();
    }
  }
  if (open) throw FormatError("unclosed message keyword tag");
  return result;
}
}  // namespace Impacto::Saekano
