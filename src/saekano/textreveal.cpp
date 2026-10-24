#include "textreveal.h"

#include "commands.h"

#include <algorithm>
#include <bit>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Impacto::Saekano {
TextFormatEnvironment TextFormatEnvironment::Decode(
    std::span<const uint8_t> bytes) {
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
void VoiceTextReveal::Prepare(std::string_view cp932,
                              const TextFormatEnvironment& format,
                              uint64_t now) {
  if (format.CharacterMaps.size() < 2 || format.Following[3].empty() ||
      format.CharacterMaps[0].size() != format.CharacterMaps[1].size())
    throw FormatError("unknown text format mapping");
  VoiceTextReveal next;
  next.Text = DecodeCp932(cp932);
  for (auto& c : next.Text) {
    const auto at = format.CharacterMaps[0].find(c);
    if (at != std::u16string::npos) c = format.CharacterMaps[1][at];
  }
  const auto nul = next.Text.find(u'\0');
  if (nul != std::u16string::npos) next.Text.resize(nul);
  if (next.Text.find_first_of(format.Tokens[2]) != std::u16string::npos)
    throw UnsupportedCommand("inline text directives require their parser");
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
  Durations = durations;
  Ratio = durations.Selected
              ? float(durations.Reference) / float(durations.Selected)
              : 0;
  Epoch = now;
  Started = true;
}
void VoiceTextReveal::Update(uint64_t now) {
  if (!Started) throw UnsupportedCommand("text reveal has not started");
  if (Revealed) return;
  int32_t count = int32_t(Glyphs.size());
  const bool instant = Durations.Selected == 0;
  if (!instant) {
    if (Ratio != 0) {
      // Preserve native low32 multiplication before conversion to float.
      const auto product = uint32_t(now - Epoch) * uint32_t(Glyphs.size());
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
void VoiceTextReveal::Reveal(uint64_t now) {
  if (!Prepared) throw UnsupportedCommand("text has not been prepared");
  for (auto& glyph : Glyphs) {
    glyph.Enabled = true;
    glyph.Alpha.Start(255, 255, 0, now);
  }
  Revealed = true;
}
}  // namespace Impacto::Saekano
