#pragma once

#include "gauge.h"
#include "texttiming.h"
#include "font.h"
#include "uisprite.h"

#include <string>

namespace Impacto::Saekano {
struct TextFormatEnvironment {
  std::array<std::u16string, 6> Tokens;
  std::vector<std::u16string> CharacterMaps;
  std::vector<std::u16string> Following = std::vector<std::u16string>(4);
  size_t PrefixBytes = 0;
  static TextFormatEnvironment Decode(std::span<const uint8_t> bytes,
                                      bool readFontTokens = false);
};
std::u16string DecodeCp932(std::string_view text);
struct PlainTextLine {
  std::u16string Text;
  uint32_t X = 0, Y = 0, Begin = 0, Line = 0;
};
struct PlainTextLayout {
  struct Glyph {
    char16_t Character = 0;
    std::array<int16_t, 2> Position{};
    int16_t Advance = 0;
    uint16_t Index = 0, Line = 0;
  };
  std::vector<PlainTextLine> Lines;
  std::vector<Glyph> Glyphs;
  float Scale = 1;
  uint16_t LineCount = 1, Height = 0, Width = 0; // Native record+194 max emitted line extent.
};
// Prepared/normalized UTF16, no inline style ranges; explicit positive scale.
// Native175d4..17b78 line traversal, kinsoku and utility-font measurements.
PlainTextLayout LayoutPlainText(std::u16string_view prepared,
                                const TextFormatEnvironment& format,
                                const GsfFont& font,
                                const TextLayoutMetrics& metrics, int32_t width,
                                float scale, uint32_t utilitySize,
                                uint32_t nominalSize);
// Fully revealed, plain mode3 glyph path used by the confirmation prompt.
std::vector<UiTextBlit> BuildPlainTextDraws(const PlainTextLayout& layout,
                                            std::array<int32_t, 2> origin,
                                            uint32_t utilitySize,
                                            std::array<uint8_t, 4> color,
                                            uint8_t opacity,
                                            bool shadow = true);
struct TextGlyphReveal {
  bool Enabled = false;
  ScriptLinearTween Alpha;
};
struct TextColorRange {
  size_t Begin = 0, End = 0;
  std::array<uint8_t, 3> Rgb{};
};
struct TextQueueMarker {
  int32_t Before = -1;  // Native zero-based body index immediately before tag.
  uint32_t Lane = 0;
  int32_t VoiceTime = 0;
};
struct OrdinaryMessageText {
  struct Keyword {size_t Begin=0,End=0;int32_t Id=-1;};
  std::u16string Name, Body;
  bool InheritedName = false, NameEnabled = false;
  std::vector<TextQueueMarker> QueueMarkers;
  std::vector<Keyword> Keywords;
};
// Shared parse-only representation. Timed reveal/lane consumption must be bound
// before a frontend admits QueueMarkers; plain Split retains its current guard.
OrdinaryMessageText ParseOrdinaryMessageControls(
    std::u16string_view source, const TextFormatEnvironment& format);
// Native16d16 ordinary name/body parsing before glyph layout. Inline directives
// require the full control-tag parser and are rejected by this plain path.
OrdinaryMessageText SplitOrdinaryMessageText(
    std::u16string_view source, const TextFormatEnvironment& format,
    std::optional<std::u16string_view> previousName = std::nullopt);
// The native mode0 path for text with no inline timing/format directives.
// Original profile responses use plain text and the authored line-break marker.
// Font metrics/wrapping are separate; they do not alter this glyph count/order.
struct VoiceTextReveal {
  struct Segment {
    int32_t Current = 0, GlyphCount = 0, Duration = 0, Offset = 0;
  };
  bool Prepared = false, Started = false, Revealed = false,
       PerCharacter = false;
  std::u16string Text;
  std::vector<std::u16string> Lines;
  std::vector<TextGlyphReveal> Glyphs;
  std::vector<TextColorRange> Colors;
  std::vector<TextQueueMarker> QueueMarkers;
  std::vector<bool> QueueFired;
  std::vector<Segment> Segments;
  size_t SegmentIndex = 0;
  VoiceTextDurations Durations;
  float Ratio = 0;
  uint64_t Epoch = 0;
  void Prepare(std::string_view cp932, const TextFormatEnvironment& format,
               uint64_t now);
  void PrepareUtf16(std::u16string_view text,
                    const TextFormatEnvironment& format, uint64_t now,
                    bool fontTags = false);
  void Start(VoiceTextDurations durations, uint64_t now);
  void StartCharacters(uint32_t interval, uint64_t now);
  void ConfigureQueueMarkers(std::span<const TextQueueMarker> markers);
  void Update(uint64_t now, int32_t voicePosition = -1);
  // Called after Update, including when text has been manually revealed.
  std::vector<uint32_t> ConsumeVoiceQueue(int32_t voicePosition);
  void Reveal(uint64_t now);
};
// Prepared mode0/mode1 glyphs, per-glyph alpha and FONT RGB ranges. Layout and
// reveal must describe the same normalized text. Ruby/keyword/pause directives
// and colored explicit-break ranges remain outside this profile subset.
std::vector<UiTextBlit> BuildRevealingTextDraws(
    const PlainTextLayout& layout, const VoiceTextReveal& reveal,
    const TextFormatEnvironment& format, std::array<int32_t, 2> origin,
    uint32_t utilitySize, std::array<uint8_t, 4> color, uint8_t opacity,
    bool shadow = true);
}  // namespace Impacto::Saekano
