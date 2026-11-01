#include "album.h"
#include "commands.h"

#include <bit>
#include <string>

namespace Impacto::Saekano {
namespace {
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
  PayloadReader reader(bytes);
  AlbumEnvironment result;
  for(auto& flag:result.Flags)flag=reader.U8();
  for(auto& word:result.ColorWords)word=reader.U32();
  for(auto& word:result.SharedWords)word=reader.I32();
  for(size_t i=0;i<2;++i)result.SharedSprites[i]=UiSpriteDefinition::ReadLegacy(reader);
  result.StretchSize={reader.I32(),reader.I32()};
  for(size_t i=2;i<6;++i)result.SharedSprites[i]=UiSpriteDefinition::ReadLegacy(reader);
  for(auto& word:result.CancelRectangle)word=reader.I32();
  for(auto& point:result.SharedPoints)point={reader.I32(),reader.I32()};
  result.SharedScale=std::bit_cast<float>(reader.U32());
  result.SharedValue=reader.I32();result.FadeDuration=reader.I32();
  result.SavePrompt=reader.String();
  result.Backdrop=UiSpriteDefinition::ReadLegacy(reader);
  for(auto& sprite:result.SectionHeaders)sprite=UiSpriteDefinition::ReadLegacy(reader);
  for(auto& sprite:result.Buttons)sprite=UiSpriteDefinition::ReadLegacy(reader);
  result.SelectedMarker=UiSpriteDefinition::ReadLegacy(reader);
  result.SelectedOffset=reader.I32();
  const auto groups=reader.U32();reader.CheckCount(groups,4);
  if(groups>1000000)throw FormatError("album environment groups exceed budget");
  for(uint32_t i=0;i<groups;++i) {
    const auto count=reader.U32();reader.CheckCount(count,16);
    if(count>1000000)throw FormatError("album environment rectangles exceed budget");
    auto& rectangles=result.ButtonRectangles.emplace_back();
    for(uint32_t j=0;j<count;++j)rectangles.push_back({reader.I32(),reader.I32(),reader.I32(),reader.I32()});
  }
  const auto labels=reader.U32();reader.CheckCount(labels,4);
  if(labels>1000000)throw FormatError("album environment labels exceed budget");
  for(uint32_t i=0;i<labels;++i)result.MenuLabels.push_back(reader.String());
  result.MenuTextScale=std::bit_cast<float>(reader.U32());
  result.MenuTextPosition={reader.I32(),reader.I32()};
  result.ImageColumns=reader.U32();
  // Native has twenty scratch slots; preserve the safe existing host limit.
  if(result.ImageColumns>20)throw FormatError("album image column count exceeds native scratch capacity");
  result.PrefixBytes=bytes.size()-reader.Remaining();
  return result;
}
std::vector<AlbumEntry> DecodeAlbumEntries(std::span<const uint8_t> bytes,
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
  std::vector<AlbumEntry> result;
  size_t imageCount=0;
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
      if (result.size() >= 1000000)
        throw FormatError("album entries exceed allocation budget");
      if (images.size() > 1000000-imageCount)
        throw FormatError("album requirements exceed allocation budget");
      imageCount+=images.size();
      result.push_back({title,Numeric(category),std::move(images),{Numeric(first),Numeric(second)}});
    }
    tokens.FinishRow();
  }
  return result;
}
std::vector<int32_t> DecodeAlbumRequirements(std::span<const uint8_t> bytes,
                                             uint32_t imageColumns) {
  std::vector<int32_t> result;
  for(const auto& entry:DecodeAlbumEntries(bytes,imageColumns))
    result.insert(result.end(),entry.Images.begin(),entry.Images.end());
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
  AlbumConfiguration result;
  result.Environment=config;
  result.Entries=DecodeAlbumEntries(cg.Data(0),config.ImageColumns);
  for(const auto& entry:result.Entries)
    result.RequiredImages.insert(result.RequiredImages.end(),entry.Images.begin(),entry.Images.end());
  return result;
}
}  // namespace Impacto::Saekano
