#include "fontface.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <set>

namespace Impacto::Saekano {
namespace {
void CheckFt(FT_Error error, const char* operation) {
  if (error)
    throw FormatError(std::string(operation) + ": FreeType " +
                      std::to_string(error));
}
}  // namespace
struct PcFontFace::Impl {
  FT_Library Library = nullptr;
  FT_Face Face = nullptr;
  Bytes Collection;
  GsfFont Cached;
  std::vector<ImagePixels> Atlases;
  std::map<char16_t, ImagePixels> Glyphs;
  std::set<char16_t> Characters;
  uint32_t Nominal = 0, Baseline = 0;
  float Points = 0;
  std::string Family;
  ~Impl() {
    if (Face) FT_Done_Face(Face);
    if (Library) FT_Done_FreeType(Library);
  }
};
PcFontFace::PcFontFace(Bytes collection, GsfFont cached, uint32_t nominalSize)
    : impl_(std::make_unique<Impl>()) {
  auto& font = *impl_;
  if (nominalSize == 0 || nominalSize > 255 || collection.empty() ||
      collection.size() > 32 * 1024 * 1024)
    throw FormatError("invalid PC font input or nominal size");
  font.Collection = std::move(collection);
  font.Cached = std::move(cached);
  font.Nominal = nominalSize;
  font.Characters.insert(font.Cached.Characters.begin(),
                         font.Cached.Characters.end());
  for (const auto& bytes : font.Cached.Atlases)
    font.Atlases.push_back(DecodeSceneImage(bytes));
  CheckFt(FT_Init_FreeType(&font.Library), "initialize font library");
  // The game uses the default user-file opener, without subfont selection.
  CheckFt(FT_New_Memory_Face(font.Library, font.Collection.data(),
                             FT_Long(font.Collection.size()), 0, &font.Face),
          "open authored TTC");
  CheckFt(FT_Select_Charmap(font.Face, FT_ENCODING_UNICODE),
          "select font Unicode map");
  if (!FT_IS_SCALABLE(font.Face))
    throw FormatError("authored TTC is not scalable");
  font.Family = font.Face->family_name ? font.Face->family_name : "";
  // Game wrapper800b2080: nominal*12.15f/22; configure115e uses128dpi.
  const float product = float(nominalSize) * 12.15f;
  font.Points = product / 22.0f;
  CheckFt(FT_Set_Char_Size(font.Face, FT_F26Dot6(font.Points * 64.0f),
                           FT_F26Dot6(font.Points * 64.0f), 128, 128),
          "set authored font size");
  const FT_Pos ascender =
      FT_MulFix(font.Face->bbox.yMax, font.Face->size->metrics.y_scale);
  font.Baseline = uint32_t(std::max<FT_Pos>(0, (ascender + 63) / 64));
  if (font.Baseline > 1024)
    throw FormatError("PC font baseline outside budget");
}
PcFontFace::~PcFontFace() = default;
const GsfFont& PcFontFace::Cached() const { return impl_->Cached; }
const ImagePixels& PcFontFace::Atlas(size_t index) const {
  return impl_->Atlases.at(index);
}
uint32_t PcFontFace::Baseline() const { return impl_->Baseline; }
uint32_t PcFontFace::NominalSize() const { return impl_->Nominal; }
float PcFontFace::PointSize() const { return impl_->Points; }
const std::string& PcFontFace::Family() const { return impl_->Family; }
const ImagePixels* PcFontFace::Glyph(char16_t character) {
  auto& font = *impl_;
  if (!font.Characters.contains(character)) return nullptr;
  if (const auto found = font.Glyphs.find(character);
      found != font.Glyphs.end())
    return &found->second;
  const auto index = FT_Get_Char_Index(font.Face, character);
  if (!index) throw FormatError("authored font charset has no TTC glyph");
  CheckFt(
      FT_Load_Glyph(font.Face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL),
      "rasterize authored glyph");
  const auto slot = font.Face->glyph;
  const auto& bitmap = slot->bitmap;
  if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256 ||
      bitmap.pitch < 0)
    throw FormatError("unsupported FreeType grayscale bitmap");
  ImagePixels pixels;
  pixels.Width = std::max<uint32_t>(
      1, uint32_t(std::max<FT_Pos>(0, (slot->advance.x + 63) / 64)));
  pixels.Height = font.Nominal;
  if (pixels.Width > 1024 || bitmap.width > 1024 || bitmap.rows > 1024 ||
      uint32_t(bitmap.pitch) < bitmap.width)
    throw FormatError("PC glyph outside raster budget");
  pixels.Rgba.resize(size_t(pixels.Width) * pixels.Height * 4);
  const int64_t top = int64_t(font.Baseline) - slot->bitmap_top;
  for (uint32_t y = 0; y < bitmap.rows; ++y) {
    const int64_t dy = top + y;
    if (dy < 0 || dy >= pixels.Height) continue;
    for (uint32_t x = 0; x < bitmap.width; ++x) {
      const int64_t dx = int64_t(slot->bitmap_left) + x;
      if (dx < 0 || dx >= pixels.Width) continue;
      const auto alpha = bitmap.buffer[size_t(y) * bitmap.pitch + x];
      const size_t at = (size_t(dy) * pixels.Width + size_t(dx)) * 4;
      pixels.Rgba[at] = pixels.Rgba[at + 1] = pixels.Rgba[at + 2] =
          alpha ? 255 : 0;
      pixels.Rgba[at + 3] = alpha;
    }
  }
  return &font.Glyphs.emplace(character, std::move(pixels)).first->second;
}
}  // namespace Impacto::Saekano
