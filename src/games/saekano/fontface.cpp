#include "fontface.h"
#include "../../renderer/legacyfontraster.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>

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
  std::map<char16_t, PvfGlyphImage> Glyphs;
  std::map<char16_t, uint32_t> CellWidths;
  uint32_t Nominal = 0, Baseline = 0, CellHeight = 0, RasterWidth = 0;
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
  const FT_Pos descender =
      FT_MulFix(font.Face->bbox.yMin, font.Face->size->metrics.y_scale);
  font.CellHeight =
      font.Baseline + uint32_t(std::max<FT_Pos>(0, (-descender + 63) / 64));
  if (font.CellHeight > 1024)
    throw FormatError("PC font baseline outside budget");

  // The precache packs full font cells even for half-width characters. It
  // truncates each accumulated 26.6 row position and excludes the last column
  // from the sampled rectangle. Draw advance is a separate half/full rule.
  const FT_Pos advance =
      FT_MulFix(font.Face->max_advance_width, font.Face->size->metrics.x_scale);
  font.RasterWidth = uint32_t(std::max<FT_Pos>(0, advance / 64));
  const uint32_t columns = std::max(1u, 900u / nominalSize);
  for (size_t i = 0; i < font.Cached.Characters.size(); ++i) {
    const int64_t column = int64_t(i % columns);
    const int64_t width =
        ((column + 1) * advance) / 64 - (column * advance) / 64 - 1;
    if (width < 1 || width > 1024)
      throw FormatError("PC font cache cell outside budget");
    font.CellWidths[font.Cached.Characters[i]] = uint32_t(width);
  }
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
const PvfGlyphImage* PcFontFace::Glyph(char16_t character) {
  auto& font = *impl_;
  const auto cell = font.CellWidths.find(character);
  if (cell == font.CellWidths.end()) return nullptr;
  if (const auto found = font.Glyphs.find(character);
      found != font.Glyphs.end())
    return &found->second;
  const auto index = FT_Get_Char_Index(font.Face, character);
  if (!index) throw FormatError("authored font charset has no TTC glyph");
  CheckFt(FT_Load_Glyph(font.Face, index,
                        FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL),
          "load authored glyph outline");
  const auto slot = font.Face->glyph;
  if (slot->format != FT_GLYPH_FORMAT_OUTLINE)
    throw FormatError("authored glyph has no scalable outline");
  PvfGlyphImage glyph;
  glyph.Source = {0, 0, int32_t(cell->second), int32_t(font.CellHeight)};
  auto& pixels = glyph.Pixels;
  // Retain the following column and transparent bottom row for end-coordinate
  // filtering instead of clamping to the last source texel. Raster clipping
  // uses the integer full-cell width, independently of each sampled rectangle.
  pixels.Width = cell->second + 1;
  pixels.Height = font.CellHeight + 1;
  if (pixels.Width > 1024) throw FormatError("PC glyph outside raster budget");
  pixels.Rgba.resize(size_t(pixels.Width) * pixels.Height * 4);
  std::vector<uint8_t> coverage(size_t(pixels.Width) * pixels.Height);
  FT_Bitmap bitmap{};
  bitmap.width = font.RasterWidth;
  bitmap.rows = font.CellHeight;
  bitmap.pitch = int(pixels.Width);
  bitmap.buffer = coverage.data();
  bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
  bitmap.num_grays = 256;
  FT_Outline_Translate(&slot->outline, 0,
                       FT_Pos((font.CellHeight - font.Baseline) * 64));
  CheckFt(ImpactoRenderLegacyFontOutline(&slot->outline, &bitmap),
          "rasterize authored glyph");
  for (size_t i = 0; i < coverage.size(); ++i) {
    const auto alpha = coverage[i];
    const size_t at = i * 4;
    pixels.Rgba[at] = pixels.Rgba[at + 1] = pixels.Rgba[at + 2] =
        alpha ? 255 : 0;
    pixels.Rgba[at + 3] = alpha;
  }
  return &font.Glyphs.emplace(character, std::move(glyph)).first->second;
}
}  // namespace Impacto::Saekano
