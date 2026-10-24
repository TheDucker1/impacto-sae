#include "album.h"

#include <bit>
#include <string>

namespace Impacto::Saekano {
namespace {
class Cursor {
 public:
  explicit Cursor(std::span<const uint8_t> data) : data_(data) {}
  void Skip(size_t count) {
    if (count > data_.size() - position_)
      throw FormatError("truncated album environment prefix");
    position_ += count;
  }
  uint32_t U32() {
    const auto start = position_;
    Skip(4);
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
      value |= uint32_t(data_[start + i]) << (8 * i);
    return value;
  }
  uint32_t Count(size_t stride) {
    const auto count = U32();
    if (count > 1000000 || count > (data_.size() - position_) / stride)
      throw FormatError("album environment collection exceeds input/budget");
    return count;
  }
  void String() { Skip(size_t(Count(2)) * 2); }
  size_t Position() const { return position_; }

 private:
  std::span<const uint8_t> data_;
  size_t position_ = 0;
};
int32_t Numeric(std::u16string_view text) {
  uint32_t value = 0;
  bool negative = false;
  for (const auto c : text) {
    if (c >= u'0' && c <= u'9') value = value * 10u + uint32_t(c - u'0');
    if (c == u'-') negative = true;
  }
  return std::bit_cast<int32_t>(negative ? 0u - value : value);
}
// Matches native80193fdc. Only CRLF marks a row boundary; bare LF or CR is a
// token delimiter. Quotes may contain commas/newlines; doubled quotes collapse.
class Tokens {
 public:
  explicit Tokens(std::u16string text) : text_(std::move(text)) {}
  bool Done() const { return position_ == text_.size(); }
  void BeginRow() { rowEnded_ = Done(); }
  std::u16string Next() {
    std::u16string result;
    if (rowEnded_) return result;
    while (!Done()) {
      const auto c = text_[position_++];
      if (c == u'"') {
        while (!Done()) {
          const auto q = text_[position_++];
          if (q != u'"')
            result.push_back(q);
          else if (!Done() && text_[position_] == u'"') {
            ++position_;
            result.push_back(u'"');
          } else
            break;
        }
      } else if (c == u',' || c == u'\r' || c == u'\n') {
        if (c == u'\r' && !Done() && text_[position_] == u'\n') {
          ++position_;
          rowEnded_ = true;
        }
        rowEnded_ = rowEnded_ || Done();
        return result;
      } else
        result.push_back(c);
    }
    rowEnded_ = true;
    return result;
  }
  void FinishRow() {
    while (!rowEnded_) Next();
  }

 private:
  std::u16string text_;
  size_t position_ = 0;
  bool rowEnded_ = false;
};
}  // namespace

AlbumEnvironment AlbumEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() > 32 * 1024 * 1024)
    throw FormatError("album environment exceeds file budget");
  Cursor cursor(bytes);
  // Neutral serialized fields preceding global8025d224. Native UI descriptor
  // reader801bf4e8 consumes I16 + eight words (34 serialized bytes).
  cursor.Skip(3 + 3 * 4 + 2 * 4);
  cursor.Skip(2 * 34 + 8 + 4 * 34 + 16 + 2 * 8 + 4 + 2 * 4);
  cursor.String();
  cursor.Skip(10 * 34 + 4);
  const auto groups = cursor.Count(4);
  for (uint32_t i = 0; i < groups; ++i)
    cursor.Skip(size_t(cursor.Count(16)) * 16);
  const auto strings = cursor.Count(4);
  for (uint32_t i = 0; i < strings; ++i) cursor.String();
  cursor.Skip(4 + 8);
  const auto columns = cursor.U32();
  // Native has twenty scratch slots. Reject malformed configuration instead
  // of reproducing its potential stack overwrite.
  if (columns > 20)
    throw FormatError(
        "album image column count exceeds native scratch capacity");
  return {columns, cursor.Position()};
}
std::vector<int32_t> DecodeAlbumRequirements(std::span<const uint8_t> bytes,
                                             uint32_t imageColumns) {
  if (imageColumns > 20)
    throw FormatError(
        "album image column count exceeds native scratch capacity");
  if (bytes.size() > 32 * 1024 * 1024 || bytes.size() % 2 || bytes.size() < 2 ||
      bytes[0] != 0xff || bytes[1] != 0xfe)
    throw FormatError("expected bounded UTF16LE album text with BOM");
  std::u16string text;
  text.reserve(bytes.size() / 2 - 1);
  for (size_t i = 2; i < bytes.size(); i += 2)
    text.push_back(char16_t(uint16_t(bytes[i]) | uint16_t(bytes[i + 1]) << 8));
  Tokens tokens(std::move(text));
  std::vector<int32_t> result;
  while (!tokens.Done()) {
    tokens.BeginRow();
    const auto enabled = tokens.Next();
    // Raw800bc634..644 compares lengths after an equal common prefix.
    if (enabled != u"ON") {
      tokens.FinishRow();
      continue;
    }
    const auto title = tokens.Next();
    if (title.empty()) {
      tokens.FinishRow();
      continue;
    }
    const auto category = tokens.Next();
    if (category.empty()) {
      tokens.FinishRow();
      continue;
    }
    std::vector<int32_t> images;
    for (uint32_t i = 0; i < imageColumns; ++i) {
      const auto field = tokens.Next();
      if (!field.empty()) {
        const auto value = Numeric(field);
        if (value >= 0) images.push_back(value);
      }
    }
    const auto first = tokens.Next(), second = tokens.Next();
    if (Numeric(category) >= 0 && !first.empty() && !second.empty() &&
        Numeric(first) >= 0 && Numeric(second) >= 0) {
      if (images.size() > 1000000 - result.size())
        throw FormatError("album requirements exceed allocation budget");
      result.insert(result.end(), images.begin(), images.end());
    }
    tokens.FinishRow();
  }
  return result;
}
AlbumConfiguration LoadAlbumConfiguration(
    Bytes firstDat, std::span<const uint8_t> environment) {
  if (firstDat.size() > 64 * 1024 * 1024)
    throw FormatError("first archive exceeds metadata budget");
  const auto config = AlbumEnvironment::Decode(environment);
  const GpdaArchive first(std::move(firstDat));
  if (first.Entries().empty() || first.Entries()[0].Type != 0)
    throw FormatError("unsupported first.dat CG metadata section");
  const GpdaArchive cg(first.Data(0));
  if (cg.Entries().empty() || cg.Entries()[0].Type != 0)
    throw FormatError("unsupported CG album text section");
  return {config, DecodeAlbumRequirements(cg.Data(0), config.ImageColumns)};
}
}  // namespace Impacto::Saekano
