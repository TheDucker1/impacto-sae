#include "textreveal.h"

#include "commands.h"
#include "messagekeywords.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Impacto::Saekano {
TextFormatEnvironment TextFormatEnvironment::Decode(
    std::span<const uint8_t> bytes, bool readFontTokens) {
  if (bytes.size() < 3) throw FormatError("truncated text format flags");
  TextFormatEnvironment result;
  PayloadReader reader(bytes.subspan(3));
  for (auto& token : result.Tokens) token = reader.String();
  const auto count = reader.U32();
  reader.CheckCount(count, 4);
  if (count < 2 || count > 4096)
    throw FormatError("invalid text character mapping count");
  for (uint32_t i = 0; i < count; ++i)
    result.CharacterMaps.push_back(reader.String());
  result.Following.resize(readFontTokens ? 27 : 4);
  for (auto& token : result.Following) token = reader.String();
  if (result.CharacterMaps[0].size() != result.CharacterMaps[1].size() ||
      result.Following[3].empty())
    throw FormatError("invalid text mapping or line break");
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
std::u16string DecodeCp932(std::string_view text) {
  if (text.size() > 1024 * 1024)
    throw FormatError("CP932 text exceeds input budget");
  text = text.substr(0, text.find('\0'));
  if (text.empty()) return {};
#ifdef _WIN32
  const auto count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text.data(),
                                         int(text.size()), nullptr, 0);
  if (!count) throw FormatError("invalid CP932 text");
  std::wstring wide(size_t(count), L'\0');
  if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text.data(),
                          int(text.size()), wide.data(), count) != count)
    throw FormatError("CP932 conversion failed");
  return std::u16string(wide.begin(), wide.end());
#else
  throw UnsupportedCommand(
      "CP932 conversion service is unavailable on this host");
#endif
}
PlainTextLayout LayoutPlainText(std::u16string_view text,
    const TextFormatEnvironment& format, const GsfFont& font,
    const TextLayoutMetrics& metrics, int32_t width, float scale,
    uint32_t utilitySize, uint32_t nominalSize) {
  if (format.Following.size() < 6 || format.Following[3].empty() ||
      format.Tokens[4].size() < 2 || width <= 0 || !std::isfinite(scale) ||
      scale <= 0 || text.size() > 65535)
    throw FormatError("invalid plain text layout inputs");
  text = text.substr(0, text.find(u'\0'));
  if (text.find_first_of(format.Tokens[2]) != std::u16string::npos)
    throw UnsupportedCommand("styled text requires layout ranges");
  PlainTextLayout result;
  result.Scale = scale;
  result.LineCount = 0;
  uint32_t x = 0, y = 0;
  uint32_t line = 0;
  size_t start = 0;
  const auto advance = metrics.LineAdvance(scale);
  if (advance < 0) throw FormatError("negative plain text line advance");
  auto emit = [&](size_t end) {
    if (end < start || end > text.size())
      throw FormatError("invalid plain text line extent");
    result.Lines.push_back({std::u16string(text.substr(start, end - start)), 0,
                            y, uint32_t(start), line++});
    result.LineCount = uint16_t(line);
    y += uint32_t(advance);
    result.Height = uint16_t(y);
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const auto character =
        text[i] == format.Tokens[4][0] ? format.Tokens[4][1] : text[i];
    const auto measured = MeasurePvfText(
        font, std::u16string_view(&character, 1), utilitySize, nominalSize);
    const float scaled = float(measured) * scale;
    if (scaled >= 2147483648.0f)
      throw FormatError("plain text glyph width exceeds layout range");
    const auto glyphWidth = uint32_t(int32_t(scaled));
    const bool explicitBreak = text[i] == format.Following[3][0];
    if (explicitBreak) {
      emit(i);
      start = i + 1;
      x = 0;
    }
    if (uint64_t(x) + glyphWidth > uint32_t(width)) {
      size_t split = i;
      while (split > start + 1 &&
             format.Following[4].find(text[split]) != std::u16string::npos)
        --split;
      const auto before = split == 0 ? 0 : split - 1;
      size_t end = before;
      while (end > start + 1 &&
             format.Following[5].find(text[end]) != std::u16string::npos)
        --end;
      if (end != before) split = end + 1;
      emit(split);
      start = split;
      i = split;
      x = 0;
    }
    if (!explicitBreak) x += glyphWidth;
  }
  if (start < text.size()) emit(text.size());
  if (!result.LineCount) result.LineCount = 1;
  // Original14ffa measures the emitted character itself. Its quote-width
  // substitution belongs only to the line-break traversal above.
  for (const auto& row : result.Lines) {
    uint32_t glyphX = row.X;
    for (size_t i = 0; i < row.Text.size(); ++i) {
      const auto character = row.Text[i];
      const auto measured = MeasurePvfText(
          font, std::u16string_view(&character, 1), utilitySize, nominalSize);
      const auto rawAdvance = std::bit_cast<int16_t>(uint16_t(measured));
      result.Glyphs.push_back({character,
                               {std::bit_cast<int16_t>(uint16_t(glyphX)),
                                std::bit_cast<int16_t>(uint16_t(row.Y))},
                               rawAdvance,
                               uint16_t(row.Begin + i),
                               uint16_t(row.Line)});
      const float scaled = float(rawAdvance) * scale;
      const int32_t step = scaled >= 2147483648.0f    ? INT32_MAX
                           : scaled <= -2147483648.0f ? INT32_MIN
                                                      : int32_t(scaled);
      glyphX += uint32_t(step);
    }
  }
  return result;
}
std::vector<UiTextBlit> BuildPlainTextDraws(const PlainTextLayout& layout,
                                            std::array<int32_t, 2> origin,
                                            uint32_t utilitySize,
                                            std::array<uint8_t, 4> color,
                                            uint8_t opacity, bool shadow) {
  if (utilitySize > uint32_t(INT32_MAX) || !std::isfinite(layout.Scale) ||
      layout.Scale <= 0)
    throw FormatError("invalid plain text drawing scale");
  const float scaled = float(utilitySize) * layout.Scale;
  const uint32_t size = scaled >= 4294967296.0f ? UINT32_MAX : uint32_t(scaled);
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  std::vector<UiTextBlit> result;
  for (const auto& glyph : layout.Glyphs) {
    const std::array<int32_t, 2> at{add(origin[0], glyph.Position[0]),
                                    add(origin[1], glyph.Position[1])};
    const auto text = std::u16string(1, glyph.Character);
    // Native emits both calls even when alpha is zero.
    result.push_back({text,
                      {add(at[0], 1), add(at[1], 1)},
                      size,
                      {0, 0, 0, uint8_t(shadow ? opacity : 0)}});
    color[3] = opacity;
    result.push_back({text, at, size, color});
  }
  return result;
}
OrdinaryMessageText SplitOrdinaryMessageText(
    std::u16string_view source, const TextFormatEnvironment& format,
    std::optional<std::u16string_view> previousName) {
  if (format.CharacterMaps.size() < 2 || format.Following.size() < 3 ||
      format.Following[0].size() < 2 || format.Following[1].empty() ||
      format.Following[2].size() < 2 || format.Tokens[2].empty() ||
      format.CharacterMaps[0].size() != format.CharacterMaps[1].size())
    throw FormatError("ordinary message format is incomplete");
  if (source.size() > 1024 * 1024)
    throw FormatError("ordinary message exceeds budget");
  std::u16string normalized(source.substr(0, source.find(u'\0')));
  for (auto& c : normalized) {
    const auto at = format.CharacterMaps[0].find(c);
    if (at != std::u16string::npos) c = format.CharacterMaps[1][at];
  }
  const auto nul = normalized.find(u'\0');
  if (nul != std::u16string::npos) normalized.resize(nul);
  if (normalized.find_first_of(format.Tokens[2]) != std::u16string::npos)
    throw UnsupportedCommand(
        "ordinary inline message directives require their parser");
  OrdinaryMessageText result;
  result.InheritedName = previousName.has_value();
  if (previousName) {
    result.Name = *previousName;
    result.NameEnabled = true;
  } else {
    const auto quote = normalized.find(format.Following[1]);
    if (quote != std::u16string::npos) {
      result.Name = normalized.substr(0, quote);
      result.NameEnabled = true;
    }
  }
  // Native strips quote characters from asides immediately following a name.
  if (!result.Name.empty() && result.Name.size() + 1 < normalized.size() &&
      format.Following[0].substr(0, 2).find(
          normalized[result.Name.size() + 1]) != std::u16string::npos) {
    const auto& quotes = format.Following[2];
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
                                    [&](char16_t c) {
                                      return c == quotes[0] || c == quotes[1];
                                    }),
                     normalized.end());
  }
  result.Body =
      result.InheritedName ? normalized : normalized.substr(result.Name.size());
  return result;
}
OrdinaryMessageText ParseOrdinaryMessageControls(
    std::u16string_view source, const TextFormatEnvironment& format) {
  if (source.size() > 1024 * 1024) throw FormatError("ordinary message exceeds budget");
  source = source.substr(0, source.find(u'\0'));
  if (format.Tokens[2].empty() || source.find_first_of(format.Tokens[2]) == source.npos)
    return SplitOrdinaryMessageText(source, format);
  if (format.Following.size() < 19 || format.Following[15].empty() ||
      format.Following[16].size() != 1 || format.Following[18].empty() ||
      format.Tokens[0].size() != 1 || format.Tokens[1].size() != 1)
    throw UnsupportedCommand("inline queue format tokens are missing");
  const auto number = [](std::u16string_view word) {
    uint32_t value = 0; bool negative = false;
    for (const auto c : word) {
      if (c >= u'0' && c <= u'9') value = value * 10u + uint32_t(c - u'0');
      if (c == u'-') negative = true;
    }
    return std::bit_cast<int32_t>(negative ? 0u - value : value);
  };
  std::u16string plain;
  std::vector<TextQueueMarker> markers;
  const auto ids=OriginalMessageKeywords(std::u16string(source));
  std::vector<OrdinaryMessageText::Keyword> keywords;
  std::optional<OrdinaryMessageText::Keyword> keyword;
  const auto glyphCount=[&](){return plain.size()-size_t(std::count(plain.begin(),plain.end(),format.Following[3][0]));};
  for (size_t cursor = 0; cursor < source.size();) {
    const auto ch = source[cursor];
    if (ch == format.Tokens[1][0]) throw FormatError("unmatched inline closing delimiter");
    if (ch != format.Tokens[0][0]) { plain.push_back(ch); ++cursor; continue; }
    const auto end = source.find(format.Tokens[1][0], cursor + 1);
    if (end == source.npos) throw FormatError("unterminated inline directive");
    auto tag = std::u16string(source.substr(cursor + 1, end - cursor - 1));
    for(auto& value:tag)if(value>=u'a' && value<=u'z')value-=u'a'-u'A';
    if(tag.starts_with(format.Following[11]+u" ")) {
      if(keyword || keywords.size()>=ids.size())throw FormatError("invalid ordinary keyword range");
      keyword=OrdinaryMessageText::Keyword{glyphCount(),0,ids[keywords.size()]};cursor=end+1;continue;
    }
    if(tag==format.Following[12]) {
      if(!keyword)throw FormatError("ordinary keyword close without start");
      keyword->End=glyphCount();keywords.push_back(*keyword);keyword.reset();cursor=end+1;continue;
    }
    const auto space = tag.find(u' ');
    if (space == tag.npos || tag.substr(0, space) != format.Following[15])
      throw UnsupportedCommand("ordinary inline directive is not bound");
    const auto command = tag.substr(space + 1);
    const auto comma = format.Following[16][0];
    const auto first = command.find(comma), second = command.find(comma, first == command.npos ? 0 : first + 1);
    if (first == command.npos || second == command.npos || command.substr(0, first) != format.Following[18] ||
        command.find(comma, second + 1) != command.npos || command.find(u' ') != command.npos)
      throw UnsupportedCommand("inline SP command is not bound");
    const auto lane = number(command.substr(first + 1, second - first - 1));
    const auto time = number(command.substr(second + 1));
    if (lane < 0 || lane >= 5 || time < 0)
      throw UnsupportedCommand("inline queue lane/time outside supported range");
    markers.push_back({int32_t(plain.size()) - 1, uint32_t(lane), time});
    cursor = end + 1;
  }
  auto parsed = SplitOrdinaryMessageText(plain, format);
  if (parsed.Body.size() != plain.size() - parsed.Name.size())
    throw UnsupportedCommand("inline controls with aside name stripping are not bound");
  for (auto& marker : markers) {
    marker.Before -= int32_t(parsed.Name.size());
    if (marker.Before < -1) throw UnsupportedCommand("inline control inside speaker name");
  }
  parsed.QueueMarkers = std::move(markers);
  if(keyword || keywords.size()!=ids.size())throw FormatError("unclosed ordinary keyword range");
  for(auto& range:keywords) {
    if(range.Begin<parsed.Name.size() || range.End<range.Begin)throw FormatError("keyword overlaps ordinary name");
    range.Begin-=parsed.Name.size();range.End-=parsed.Name.size();
  }
  parsed.Keywords=std::move(keywords);
  return parsed;
}
void VoiceTextReveal::Prepare(std::string_view cp932,
                              const TextFormatEnvironment& format,
                              uint64_t now) {
  PrepareUtf16(DecodeCp932(cp932), format, now);
}
void VoiceTextReveal::PrepareUtf16(std::u16string_view source,
                                   const TextFormatEnvironment& format,
                                   uint64_t now, bool fontTags) {
  if (format.CharacterMaps.size() < 2 || format.Following.size() < 4 ||
      format.Following[3].empty() ||
      format.CharacterMaps[0].size() != format.CharacterMaps[1].size())
    throw FormatError("unknown text format mapping");
  VoiceTextReveal next;
  if (source.size() > 1024 * 1024)
    throw FormatError("UTF16 text exceeds input budget");
  next.Text = source.substr(0, source.find(u'\0'));
  for (auto& c : next.Text) {
    const auto at = format.CharacterMaps[0].find(c);
    if (at != std::u16string::npos) c = format.CharacterMaps[1][at];
  }
  const auto nul = next.Text.find(u'\0');
  if (nul != std::u16string::npos) next.Text.resize(nul);
  if (next.Text.find_first_of(format.Tokens[2]) != std::u16string::npos) {
    if (!fontTags || format.Following.size() < 27 || format.Tokens[0].empty() ||
        format.Tokens[1].empty())
      throw UnsupportedCommand("inline text directives require their parser");
    const auto open = format.Tokens[0][0], close = format.Tokens[1][0];
    const auto prefix = format.Following[6] + u" " + format.Following[20] +
                        format.Following[23] + u"#";
    std::u16string plain;
    size_t glyph = 0;
    std::optional<TextColorRange> color;
    for (size_t i = 0; i < next.Text.size();) {
      if (next.Text[i] == open) {
        const auto end = next.Text.find(close, i + 1);
        if (end == std::u16string::npos)
          throw FormatError("unterminated text directive");
        const auto tag =
            std::u16string_view(next.Text).substr(i + 1, end - i - 1);
        if (tag == format.Following[7]) {
          if (!color) throw FormatError("font close without color range");
          color->End = glyph;
          next.Colors.push_back(*color);
          color.reset();
        } else if (tag.starts_with(prefix) && tag.size() == prefix.size() + 6) {
          if (color)
            throw UnsupportedCommand(
                "nested font directives are not implemented");
          TextColorRange range;
          range.Begin = glyph;
          for (size_t channel = 0; channel < 3; ++channel) {
            uint8_t value = 0;
            for (size_t digit = 0; digit < 2; ++digit) {
              const auto c = tag[prefix.size() + channel * 2 + digit];
              const auto n = c >= u'0' && c <= u'9'   ? c - u'0'
                             : c >= u'A' && c <= u'F' ? c - u'A' + 10
                             : c >= u'a' && c <= u'f' ? c - u'a' + 10
                                                      : 16;
              if (n > 15) throw FormatError("invalid font RGB color");
              value = uint8_t(value * 16 + n);
            }
            range.Rgb[channel] = value;
          }
          color = range;
        } else
          throw UnsupportedCommand("unsupported text directive");
        i = end + 1;
      } else {
        const auto c = next.Text[i++];
        if (c == close) throw FormatError("unmatched text directive delimiter");
        plain.push_back(c);
        if (c != format.Following[3][0]) ++glyph;
      }
    }
    if (color) throw FormatError("unclosed font color range");
    next.Text = std::move(plain);
  }
  next.Lines.emplace_back();
  size_t glyphs = 0;
  for (const auto c : next.Text) {
    if (c == format.Following[3][0])
      next.Lines.emplace_back();
    else {
      next.Lines.back().push_back(c);
      ++glyphs;
    }
  }
  if (glyphs > 65535) throw FormatError("text exceeds native glyph count");
  next.Glyphs.resize(glyphs);
  for (auto& glyph : next.Glyphs) glyph.Alpha.Start(0, 255, 200, now);
  next.Prepared = true;
  *this = std::move(next);
}
void VoiceTextReveal::Start(VoiceTextDurations durations, uint64_t now) {
  if (!Prepared || Started)
    throw UnsupportedCommand(
        "text requires fresh preparation before reveal start");
  std::vector<Segment> segments;
  int32_t previousGlyph = 0, previousTime = 0;
  for (const auto& marker : QueueMarkers) {
    if (durations.Selected && uint32_t(marker.VoiceTime) > durations.Selected)
      throw UnsupportedCommand("queue threshold exceeds selected text duration");
    const auto glyphs = marker.Before - previousGlyph;
    const auto duration = marker.VoiceTime - previousTime;
    if (durations.Selected && !duration && glyphs)
      throw UnsupportedCommand("zero-time queued glyph segment is not verified");
    segments.push_back({0, glyphs, duration, previousTime});
    previousGlyph = marker.Before; previousTime = marker.VoiceTime;
  }
  if (!QueueMarkers.empty()) {
    segments.push_back({0, int32_t(Glyphs.size()) - previousGlyph,
        std::bit_cast<int32_t>(durations.Selected - uint32_t(previousTime)), previousTime});
    if (durations.Selected && !segments.back().Duration && segments.back().GlyphCount)
      throw UnsupportedCommand("zero-time queued tail is not verified");
  }
  Segments = std::move(segments);
  Durations = durations;
  Ratio = durations.Selected
              ? float(durations.Reference) / float(durations.Selected)
              : 0;
  Epoch = now;
  Started = true;
}
void VoiceTextReveal::ConfigureQueueMarkers(std::span<const TextQueueMarker> markers) {
  if (!Prepared || Started) throw UnsupportedCommand("queue markers need fresh prepared text");
  int32_t before = 0, time = 0;
  for (const auto& marker : markers) {
    if (marker.Before < before || size_t(marker.Before) >= Glyphs.size() ||
        marker.VoiceTime < time || marker.Lane >= 5)
      throw UnsupportedCommand("invalid or nonmonotonic text queue marker");
    before = marker.Before; time = marker.VoiceTime;
  }
  if (!markers.empty() && Lines.size() != 1)
    throw UnsupportedCommand("queued explicit line breaks require source-index mapping");
  QueueMarkers.assign(markers.begin(), markers.end());
  QueueFired.assign(markers.size(), false);
  SegmentIndex = 0;
}
void VoiceTextReveal::StartCharacters(uint32_t interval, uint64_t now) {
  Start({interval, interval}, now);
  PerCharacter = true;
  Ratio = 1.0f;
}
void VoiceTextReveal::Update(uint64_t now, int32_t voicePosition) {
  if (!Started) throw UnsupportedCommand("text reveal has not started");
  if (Revealed) return;
  int32_t count = int32_t(Glyphs.size());
  const bool instant = Durations.Selected == 0;
  const auto elapsed = voicePosition >= 0 ? uint32_t(voicePosition) : uint32_t(now - Epoch);
  if (!instant && PerCharacter) {
    count = std::bit_cast<int32_t>(uint32_t(now - Epoch) / Durations.Selected);
  } else if (!instant) {
    if (!Segments.empty()) {
      if (SegmentIndex >= Segments.size()) throw FormatError("text queue segment outside vector");
      auto& segment = Segments[SegmentIndex];
      const auto delta = elapsed >= uint32_t(segment.Offset) ? elapsed - uint32_t(segment.Offset) : 0u;
      if (Ratio == 0) segment.Current = segment.GlyphCount;
      else if (segment.Duration == 0) segment.Current = 0;
      else {
        const float quotient = float(delta * uint32_t(segment.GlyphCount)) / (float(segment.Duration) * Ratio);
        segment.Current = quotient >= 2147483648.0f ? std::numeric_limits<int32_t>::max() : int32_t(quotient);
      }
      uint32_t sum = 0;
      for (size_t i = 0; i <= SegmentIndex; ++i) sum += uint32_t(Segments[i].Current);
      count = std::bit_cast<int32_t>(sum);
    } else if (Ratio != 0) {
      // Preserve native low32 multiplication before conversion to float.
      const auto product = elapsed * uint32_t(Glyphs.size());
      const float quotient =
          float(product) / (float(Durations.Selected) * Ratio);
      count = quotient >= 2147483648.0f ? std::numeric_limits<int32_t>::max()
                                        : int32_t(quotient);
    }
    count = std::bit_cast<int32_t>(uint32_t(count) + 1);
  }
  const bool reached = count >= int32_t(Glyphs.size());
  const auto visited = std::min(Glyphs.size(), size_t(std::max(1, count)));
  bool finished = true;
  for (size_t i = 0; i < visited; ++i) {
    auto& glyph = Glyphs[i];
    if (!glyph.Enabled) {
      glyph.Alpha.Start(0, 255, 200, now);
      glyph.Enabled = true;
    }
    if (instant) glyph.Alpha.Start(0, glyph.Alpha.To, 0, now);
    glyph.Alpha.Tick(now);
    finished = finished && glyph.Alpha.Finished();
  }
  Revealed = reached && finished;
}
std::vector<uint32_t> VoiceTextReveal::ConsumeVoiceQueue(int32_t voicePosition) {
  std::vector<uint32_t> lanes;
  if (!Started || voicePosition < 0) return lanes;
  for (size_t i = 0; i < QueueMarkers.size(); ++i) {
    if (!QueueFired[i] && voicePosition >= QueueMarkers[i].VoiceTime) {
      QueueFired[i] = true;
      ++SegmentIndex;
      lanes.push_back(QueueMarkers[i].Lane);
    }
  }
  return lanes;
}
void VoiceTextReveal::Reveal(uint64_t now) {
  if (!Prepared) throw UnsupportedCommand("text has not been prepared");
  for (auto& glyph : Glyphs) {
    glyph.Enabled = true;
    glyph.Alpha.Start(255, 255, 0, now);
  }
  Revealed = true;
}
std::vector<UiTextBlit> BuildRevealingTextDraws(
    const PlainTextLayout& layout, const VoiceTextReveal& reveal,
    const TextFormatEnvironment& format, std::array<int32_t, 2> origin,
    uint32_t utilitySize, std::array<uint8_t, 4> color, uint8_t opacity,
    bool shadow) {
  if (!reveal.Prepared || layout.Glyphs.size() != reveal.Glyphs.size() ||
      format.Following.size() < 27 || format.Following[26].empty())
    throw FormatError("revealing draw needs matching prepared glyphs/format");
  for (const auto& range : reveal.Colors)
    if (range.Begin > range.End || range.End > reveal.Glyphs.size())
      throw FormatError("revealing text color range exceeds prepared glyphs");
  auto draws =
      BuildPlainTextDraws(layout, origin, utilitySize, color, opacity, shadow);
  for (size_t i = 0; i < layout.Glyphs.size(); ++i) {
    const auto& glyph = layout.Glyphs[i];
    if (glyph.Index >= reveal.Text.size() ||
        reveal.Text[glyph.Index] != glyph.Character)
      throw FormatError("revealing layout belongs to different prepared text");
    if (!reveal.Colors.empty() && glyph.Index != i)
      throw UnsupportedCommand("colored explicit breaks require layout ranges");
    const auto alpha = uint8_t(
        std::bit_cast<int32_t>(uint32_t(reveal.Glyphs[i].Alpha.Current) *
                               uint32_t(opacity)) /
        255);
    auto& foreground = draws[i * 2 + 1];
    if (glyph.Character == format.Following[26][0])
      foreground.Color = {255, 255, 255, opacity};
    for (const auto& range : reveal.Colors) {
      if (range.Begin <= glyph.Index && glyph.Index < range.End) {
        std::copy(range.Rgb.begin(), range.Rgb.end(), foreground.Color.begin());
        break;
      }
    }
    foreground.Color[3] = alpha;
    draws[i * 2].Color[3] = shadow ? alpha : 0;
  }
  return draws;
}
}  // namespace Impacto::Saekano
