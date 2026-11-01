#pragma once

#include "slotenv.h"
#include "slotdata.h"
#include "textreveal.h"

namespace Impacto::Saekano {
struct SavePreviewSettings {
  float FontScale = 0;
  uint32_t MaxUnits = 0;
  std::u16string Suffix;
  static SavePreviewSettings FromSlotEnvironment(const SlotEnvironment& environment);
};
// Native19016 + wrapper30338..303e0. Inputs are the formatter's prefix and
// emitted main glyphs in line order, not raw script markup or revealed text.
// No line separators are synthesized; the native limit counts UTF16 units.
std::u16string BuildSavePreview(std::u16string_view prefix,
    std::span<const PlainTextLayout::Glyph> glyphs, const SavePreviewSettings& settings);
// Own the new string before publication. Other imported/working record fields,
// including its last context snapshot and thumbnail, remain untouched.
void PublishSavePreview(NativeSlotRecord& working, std::u16string_view prefix,
    std::span<const PlainTextLayout::Glyph> glyphs, const SavePreviewSettings& settings);
// Projects the retained authored source to native record+b8, then native17d9c
// reuses the ordinary parser with no previous-record/name input,
// width1000 and the slot preview scale. These borrowed font services outlive it.
struct SavePreviewFormatter {
  const TextFormatEnvironment& Format;
  const GsfFont& Font;
  const TextLayoutMetrics& Metrics;
  uint32_t UtilitySize, NominalSize;
  SavePreviewSettings Settings;
  std::u16string Prepare(std::u16string_view source) const;
  void Publish(NativeSlotRecord& working, std::u16string_view source) const;
};
}
