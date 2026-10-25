#pragma once

#include "font.h"

namespace Impacto::Saekano {
// PC rasterization of the game's third-party PVF font through FreeType.
// Text advances and bitmap overrides remain the shared native layout rules.
class PcFontFace {
 public:
  PcFontFace(Bytes collection, GsfFont cached, uint32_t nominalSize = 36);
  ~PcFontFace();
  PcFontFace(const PcFontFace&) = delete;
  PcFontFace& operator=(const PcFontFace&) = delete;
  const GsfFont& Cached() const;
  const ImagePixels& Atlas(size_t index) const;
  // Only authored precache characters exist in native PVF mode. A missing
  // entry returns null, while the caller still advances its text cursor.
  const ImagePixels* Glyph(char16_t character);
  uint32_t Baseline() const;
  uint32_t NominalSize() const;
  float PointSize() const;
  const std::string& Family() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace Impacto::Saekano
