#include "music.h"

#include "commands.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>

namespace Impacto::Saekano {
AudioEnvironment AudioEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3) throw FormatError("truncated audio environment flags");
  AudioEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  result.OpaqueWord = reader.I32();
  const auto count = reader.U32();
  if (count > 1000000)
    throw FormatError("audio environment table exceeds allocation budget");
  reader.CheckCount(count, 4);
  for (uint32_t n = 0; n < count; ++n)
    result.OpaqueTable.push_back(reader.I32());
  // The byte is followed by a serialized packed BGRA word. Retain its bits;
  // audio uses only the final two F32 values.
  if (reader.Remaining() < 17)
    throw FormatError("truncated audio environment tail");
  const auto tail = bytes.subspan(bytes.size() - reader.Remaining());
  result.OpaqueFlag = tail[0];
  PayloadReader values(tail.subspan(1));
  result.PackedColor = values.U32();
  result.OpaqueTail = values.I32();
  result.BgmCoefficient = std::bit_cast<float>(values.U32());
  result.SeCoefficient = std::bit_cast<float>(values.U32());
  if (values.Remaining()) throw FormatError("trailing audio environment data");
  for (float value : {result.BgmCoefficient, result.SeCoefficient})
    if (!std::isfinite(value) || value < 0)
      throw FormatError("invalid audio volume coefficient");
  return result;
}

PlayBgmCommand PlayBgmCommand::Decode(std::span<const uint8_t> bytes) {
  PayloadReader reader(bytes);
  return {reader.String(), reader.I32()};
}

int32_t ParseBgmTrack(std::u16string_view text) {
  size_t cursor = 0;
  const auto space = [](char16_t c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
  };
  while (cursor < text.size() && space(text[cursor])) ++cursor;
  bool negative = false;
  if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
    negative = text[cursor++] == '-';
  uint64_t value = 0;
  const uint64_t limit = negative ? uint64_t(INT32_MAX) + 1 : INT32_MAX;
  while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
    value = std::min(limit, value * 10 + (text[cursor++] - '0'));
  }
  // Track strings in the authored game are ASCII decimal. Do not invent the
  // legacy narrow conversion for other characters that strtol could encounter.
  if (cursor < text.size() && text[cursor] > 127)
    throw UnsupportedCommand("non-ASCII BGM track conversion is unverified");
  if (!negative) return static_cast<int32_t>(value);
  return std::bit_cast<int32_t>(0u - static_cast<uint32_t>(value));
}

BgmPlayRequest BgmRequest(int32_t track, int32_t master, int32_t command,
                          float coefficient) {
  if (master < 0 || master > 100)
    throw FormatError("BGM master volume exceeds settings range");
  if (!std::isfinite(coefficient) || coefficient < 0)
    throw FormatError("invalid BGM coefficient");
  const auto product =
      std::bit_cast<int32_t>(uint32_t(master) * uint32_t(command));
  const float scaled = float(product / 100) * coefficient;
  // Native VCVT.U32.F32 saturates. Preserve unsigned percentage bits rather
  // than depending on host float-to-integer overflow behavior.
  const uint32_t volume = !(scaled > 0) ? 0
                          : double(scaled) >= UINT32_MAX
                              ? UINT32_MAX
                              : static_cast<uint32_t>(scaled);
  return {track, command, master, volume, float(volume) / 100.0f};
}

ArchiveBgmResources::ArchiveBgmResources(const std::filesystem::path& usrdir)
    : bank_(std::make_shared<AwbBank>(usrdir / "bgm.awb")) {
  std::ifstream input(usrdir / "env" / "env_game_utility.dat",
                      std::ios::binary | std::ios::ate);
  if (!input) throw FormatError("cannot open original audio environment");
  const auto size = input.tellg();
  if (size < 0 || size > 4000028)
    throw FormatError("audio environment exceeds file budget");
  input.seekg(0);
  Bytes bytes{std::istreambuf_iterator<char>(input), {}};
  if (input.bad()) throw FormatError("cannot read original audio environment");
  environment_ = AudioEnvironment::Decode(bytes);
}
void ArchiveBgmResources::Prepare(int32_t track) {
  if (track < 0) throw FormatError("negative BGM track ID");
  if (!prepared_.contains(track)) {
    const auto reader = Open(track);
    prepared_.emplace(track, reader->Info());
  }
}
std::unique_ptr<HcaWaveReader> ArchiveBgmResources::Open(int32_t track) const {
  if (track < 0) throw FormatError("negative BGM track ID");
  return std::make_unique<HcaWaveReader>(bank_, static_cast<uint32_t>(track));
}
const HcaWaveInfo& ArchiveBgmResources::Info(int32_t track) const {
  return prepared_.at(track);
}

void StartPlayBgm(SemanticState& state, const PlayBgmCommand& command,
                  BgmResources& resources) {
  const auto track = ParseBgmTrack(command.TrackText);
  if (track < 0) throw FormatError("negative BGM track ID");
  if (!state.MusicHistory.Known)
    throw UnsupportedCommand("initial played-music history is unknown");
  if (!state.BgmMasterVolume)
    throw UnsupportedCommand("initial BGM master setting is unknown");
  if (track > 1000000)
    throw FormatError("BGM history allocation budget exceeded");
  const auto request = BgmRequest(track, *state.BgmMasterVolume, command.Volume,
                                  resources.Coefficient());
  resources.Prepare(
      track);  // Bank validation and real decoder setup before mutation.
  auto& history = state.MusicHistory.Values;
  const auto status = size_t(track) < history.size()
                          ? history[size_t(track)]
                          : state.MusicHistory.Default;
  if (status != 1 && status != 2 && status != 3) {
    if (history.size() <= size_t(track))
      history.resize(size_t(track) + 1, state.MusicHistory.Default);
    history[size_t(track)] = 1;
  }
  state.BgmTrack = track;
  state.BgmCommandVolume = command.Volume;
  state.PendingEvents.push_back(
      {GameEventKind::PlayBgm, track, {}, command.Volume, request});
}
}  // namespace Impacto::Saekano
