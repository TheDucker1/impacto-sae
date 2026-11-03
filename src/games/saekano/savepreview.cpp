#include "savepreview.h"

#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
void Validate(const SavePreviewSettings& settings) {
  if (!std::isfinite(settings.FontScale) || settings.FontScale <= 0 || !settings.MaxUnits)
    throw FormatError("invalid save preview settings");
}
}
SavePreviewSettings SavePreviewSettings::FromSlotEnvironment(const SlotEnvironment& e) {
  SavePreviewSettings result{std::bit_cast<float>(e.Scalars.at(0x2cc)),
                            e.Scalars.at(0x2d0), e.Strings.at(0x580)};
  Validate(result); return result;
}
std::u16string BuildSavePreview(std::u16string_view prefix,
    std::span<const PlainTextLayout::Glyph> glyphs, const SavePreviewSettings& settings) {
  Validate(settings);
  std::u16string result(prefix);
  for (const auto& glyph : glyphs) result.push_back(glyph.Character);
  if (result.size() > settings.MaxUnits) {
    result.resize(settings.MaxUnits - 1);
    result += settings.Suffix;
  }
  return result;
}
void PublishSavePreview(NativeSlotRecord& working, std::u16string_view prefix,
    std::span<const PlainTextLayout::Glyph> glyphs, const SavePreviewSettings& settings) {
  const auto text = BuildSavePreview(prefix, glyphs, settings);
  std::vector<uint16_t> owned(text.begin(), text.end());
  working.Strings[2] = std::move(owned);
}
std::u16string SavePreviewFormatter::Prepare(std::u16string_view source) const {
  Validate(Settings);
  // The VM retains authored sources. Native menu entry instead obtains the
  // first record's +b8 normalized string, then constructs a fresh font owner.
  // Reconstruct that projection without consuming reveal/voice/queue state.
  const auto retained = ParseOrdinaryMessageControls(source, Format);
  const auto parsed = ParseOrdinaryMessageControls(retained.Name + retained.Body, Format);
  const auto layout = LayoutPlainText(parsed.Body, Format, Font, Metrics, 1000,
      Settings.FontScale, UtilitySize, NominalSize, parsed.Ruby);
  return BuildSavePreview(parsed.Name, layout.Glyphs, Settings);
}
void SavePreviewFormatter::Publish(NativeSlotRecord& working, std::u16string_view source) const {
  const auto text = Prepare(source);
  std::vector<uint16_t> owned(text.begin(), text.end());
  working.Strings[2] = std::move(owned);
}
}
