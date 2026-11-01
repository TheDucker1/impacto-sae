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
ExtrasScreenEnvironment ExtrasScreenEnvironment::DecodeThroughMovies(std::span<const uint8_t> bytes){
  ExtrasScreenEnvironment e;e.Common=AlbumEnvironment::Decode(bytes);
  PayloadReader r(bytes.subspan(e.Common.PrefixBytes));
  const auto table=[&](size_t stride,auto read){
    const auto n=r.U32();r.CheckCount(n,stride);
    std::vector<decltype(read())> result;result.reserve(n);
    for(uint32_t i=0;i<n;++i)result.push_back(read());return result;
  };
  const auto pointValue=[&]{return std::array<int32_t,2>{r.I32(),r.I32()};};
  const auto rectValue=[&]{return std::array<int32_t,4>{r.I32(),r.I32(),r.I32(),r.I32()};};
  const auto spriteValue=[&]{return UiSpriteDefinition::ReadLegacy(r);};
  const auto scalar=[&](uint16_t at){e.Scalars.emplace(at,r.U32());};
  const auto point=[&](uint16_t at){e.Points.emplace(at,pointValue());};
  const auto rect=[&](uint16_t at){e.Rectangles.emplace(at,rectValue());};
  const auto sprite=[&](uint16_t at){e.Sprites.emplace(at,spriteValue());};
  const auto string=[&](uint16_t at){e.Strings.emplace(at,r.String());};
  const auto sprites=[&](uint16_t at){e.SpriteTables.emplace(at,table(34,spriteValue));};
  sprites(0xd228);scalar(0xd238);rect(0xd23c);
  sprite(0xd24c);sprite(0xd270);sprite(0xd294);point(0xd2b8);point(0xd2c0);
  scalar(0xd2c8);scalar(0xd2cc);sprite(0xd2d0);scalar(0xd2f4);rect(0xd2f8);
  point(0xd308);scalar(0xd310);point(0xd314);sprite(0xd31c);point(0xd340);
  sprite(0xd348);point(0xd36c);sprite(0xd374);sprite(0xd398);point(0xd3bc);point(0xd3c4);
  string(0xd3cc);point(0xd404);scalar(0xd40c);point(0xd410);scalar(0xd418);sprite(0xd41c);
  string(0xd440);scalar(0xd478);sprite(0xd47c);scalar(0xd4a0);sprite(0xd4a4);
  scalar(0xd4c8);sprite(0xd4cc);sprite(0xd4f0);sprite(0xd514);scalar(0xd538);
  sprite(0xd53c);sprite(0xd560);point(0xd584);sprite(0xd58c);sprite(0xd5b0);
  sprite(0xd5d4);sprite(0xd5f8);point(0xd61c);sprite(0xd624);
  sprites(0xd648);sprites(0xd658);sprites(0xd668);rect(0xd678);rect(0xd688);
  point(0xd698);scalar(0xd6a0);scalar(0xd6a4);sprite(0xd6a8);scalar(0xd6cc);scalar(0xd6d0);
  sprite(0xd6d4);sprite(0xd6f8);e.AlbumRectangles=table(16,rectValue);
  point(0xd72c);scalar(0xd734);scalar(0xd738);point(0xd73c);scalar(0xd744);
  sprite(0xd748);point(0xd76c);scalar(0xd774);point(0xd778);point(0xd780);scalar(0xd788);
  e.MusicTracks=table(20,[&]{MusicTrack t;t.Flags=r.I32();t.Wave=r.I32();t.History=r.I32();t.DurationSeconds=r.I32();t.Title=r.String();return t;});
  string(0xd79c);string(0xd7d4);string(0xd80c);scalar(0xd844);string(0xd848);
  scalar(0xd880);scalar(0xd884);scalar(0xd888);
  e.Movies=table(8,[&]{Movie m;m.Name=r.String();m.Title=r.String();return m;});
  string(0xd89c);sprite(0xd8d4);scalar(0xd8f8);sprite(0xd8fc);sprite(0xd920);sprite(0xd944);
  rect(0xd968);point(0xd978);scalar(0xd980);rect(0xd984);rect(0xd994);scalar(0xd9a4);
  point(0xd9a8);point(0xd9b0);scalar(0xd9b8);scalar(0xd9bc);sprite(0xd9c0);scalar(0xd9e4);
  sprite(0xd9e8);sprite(0xda0c);sprite(0xda30);sprite(0xda54);point(0xda78);scalar(0xda80);
  sprite(0xda84);sprite(0xdaa8);scalar(0xdacc);scalar(0xdad0);
  e.PrefixBytes=bytes.size()-r.Remaining();return e;
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
    const auto galleryCategory = tokens.Next();
    if (Numeric(category) >= 0 && !first.empty() && !second.empty() &&
        Numeric(first) >= 0 && Numeric(second) >= 0) {
      if (result.size() >= 1000000)
        throw FormatError("album entries exceed allocation budget");
      if (images.size() > 1000000-imageCount)
        throw FormatError("album requirements exceed allocation budget");
      imageCount+=images.size();
      result.push_back({title,Numeric(category),std::move(images),{Numeric(first),Numeric(second)},Numeric(galleryCategory)});
    }
    tokens.FinishRow();
  }
  return result;
}
AlbumGalleryCatalogue::AlbumGalleryCatalogue(std::vector<AlbumEntry> entries):Entries(std::move(entries)){
  std::array<size_t,6> counts{};
  int32_t category=-1;
  for(size_t i=0;i<Entries.size();++i){
    const auto& entry=Entries[i];
    if(entry.GalleryCategory<0 || entry.GalleryCategory>=6 || entry.GalleryCategory<category)
      throw FormatError("CG gallery categories are outside the native ordered six groups");
    if(entry.Images.empty())throw FormatError("CG gallery entry has no image variants");
    if(entry.GalleryCategory!=category){
      while(Cards.size()%9)Cards.emplace_back();
      category=entry.GalleryCategory;
    }
    Cards.push_back(i);++counts[size_t(category)];
  }
  while(Cards.size()%9)Cards.emplace_back();
  int32_t page=-1;
  for(size_t i=0;i<counts.size();++i){page+=int32_t((counts[i]+8)/9);CategoryLastPage[i]=page;}
  for(size_t first=0;first<Cards.size();first+=9){
    int32_t count=0;for(size_t i=first;i<first+9;++i)if(Cards[i])++count;
    PageCardCounts.push_back(count);
  }
}
const AlbumEntry* AlbumGalleryCatalogue::Card(size_t index)const{
  return index<Cards.size() && Cards[index]?&Entries.at(*Cards[index]):nullptr;
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
