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
}
