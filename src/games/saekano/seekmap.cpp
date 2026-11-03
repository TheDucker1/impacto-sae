#include "seekmap.h"

#include "commands.h"
#include "resources.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace Impacto::Saekano {
uint32_t ResourceNameHashAscii(std::string_view name) {
  if (name.size() > 1024 * 1024)
    throw FormatError("resource name exceeds input budget");
  name = name.substr(0, name.find('\0'));
  for (unsigned char c : name)
    if (c >= 128)
      throw UnsupportedCommand("non-ASCII resource name locale is unavailable");
  const auto extension = name.rfind('.');
  if (extension != std::string_view::npos) name = name.substr(0, extension);
  uint32_t hash = 0xffffffff;
  for (unsigned char c : name) {
    if (c >= 'A' && c <= 'Z') c = uint8_t(c + ('a' - 'A'));
    hash ^= uint32_t(c) << 24;
    for (size_t i = 0; i < 8; ++i)
      hash = (hash << 1) ^ ((hash & 0x80000000) ? 0x04c11db7u : 0u);
  }
  return hash;
}
namespace {
bool Space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' ||
         c == '\f';
}
struct Node {
  ScriptSeekRecord Record;
  std::vector<Node> Children;
};
class Parser {
 public:
  explicit Parser(std::span<const uint8_t> bytes)
      : text_(reinterpret_cast<const char*>(bytes.data()), bytes.size()) {
    if (bytes.size() > 32 * 1024 * 1024)
      throw FormatError("seekmap exceeds input budget");
  }
  Node Read(size_t depth = 0) {
    if (depth > 128 || ++nodes_ > 1000000)
      throw FormatError("seekmap tree exceeds allocation/depth budget");
    auto line = Line();
    Node node;
    node.Record.Name = std::string(Token(line));
    node.Record.Offset = Number(line);
    node.Record.Size = Number(line);
    node.Record.StoredHash = Number(line);
    // Native scanf accepts four-column leaves; only a full six-field row
    // enters the flags/child branch. Omitted flags/count are not read.
    for (char c : line) {
      if (!Space(c)) {
        node.Record.Flags = Number(line);
        break;
      }
    }
    uint32_t count = 0;
    for (char c : line) {
      if (!Space(c)) {
        count = Number(line);
        break;
      }
    }
    if (count > (text_.size() - position_) / 2)
      throw FormatError("seekmap child/opaque rows exceed available input");
    // Native8009cd7e..cda2 skips exactly count raw CRLF lines for flag2.
    // Those lines are opaque; do not parse them as tree nodes or hash names.
    if (node.Record.Flags & 2) {
      if (count > 1000000)
        throw FormatError("seekmap skipped-line count exceeds budget");
      for (uint32_t i = 0; i < count; ++i) Line();
    } else {
      if (count > 1000000 - nodes_)
        throw FormatError("seekmap child count exceeds budget");
      node.Children.reserve(count);
      for (uint32_t i = 0; i < count; ++i)
        node.Children.push_back(Read(depth + 1));
    }
    return node;
  }

 private:
  std::string_view text_;
  size_t position_ = 0, nodes_ = 0;
  std::string_view Line() {
    const auto end = text_.find('\r', position_);
    if (end == std::string_view::npos || end + 1 >= text_.size() ||
        text_[end + 1] != '\n')
      throw FormatError("truncated seekmap CRLF row");
    const auto line = text_.substr(position_, end - position_);
    position_ = end + 2;
    if (line.find('\0') != std::string_view::npos)
      throw FormatError("seekmap row contains embedded NUL");
    return line;
  }
  static std::string_view Token(std::string_view& line) {
    size_t i = 0;
    while (i < line.size() && Space(line[i])) ++i;
    line.remove_prefix(i);
    i = 0;
    while (i < line.size() && !Space(line[i])) ++i;
    if (!i) throw FormatError("missing seekmap field");
    auto token = line.substr(0, i);
    line.remove_prefix(i);
    return token;
  }
  static uint32_t Number(std::string_view& line) {
    const auto token = Token(line);
    uint32_t value = 0;
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
      throw FormatError("invalid seekmap unsigned field");
    return value;
  }
};
}  // namespace
ScriptNameIndex::ScriptNameIndex(std::span<const uint8_t> seekmapText) {
  auto root = Parser(seekmapText).Read();
  if (root.Children.size() <= 4)
    throw FormatError("seekmap has no script section");
  for (auto& child : root.Children[4].Children) {
    child.Record.Hash = ResourceNameHashAscii(child.Record.Name);
    records_.push_back(std::move(child.Record));
  }
}
ScriptNameIndex ScriptNameIndex::FromFirstArchive(Bytes firstDat) {
  GpdaArchive archive(std::move(firstDat));
  if (archive.Entries().size() <= 5 || archive.Entries()[5].Type != 0)
    throw FormatError("first archive has no compressed seekmap");
  const auto text = InflateGzip(archive.Data(5), 32 * 1024 * 1024);
  return ScriptNameIndex(text);
}
int32_t ScriptNameIndex::Resolve(std::string_view name) const {
  const auto hash = ResourceNameHashAscii(name);
  for (size_t i = 0; i < records_.size(); ++i)
    if (records_[i].Hash == hash) return int32_t(i);
  return -1;
}
}  // namespace Impacto::Saekano
