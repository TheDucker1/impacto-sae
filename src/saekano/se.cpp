#include "se.h"

#include "commands.h"

#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>

namespace Impacto::Saekano {
PlaySeCommand PlaySeCommand::Decode(std::span<const uint8_t> bytes) {
  PayloadReader reader(bytes);
  PlaySeCommand result;
  result.Route = reader.U32();
  result.Track = reader.I32();
  result.Channel = reader.I32();
  result.Volume = reader.I32();
  result.Remember = reader.U32() != 0;
  result.Lane = reader.U32();
  return result;
}
BgmVolume SeVolume(int32_t master, int32_t command, float coefficient) {
  if (!std::isfinite(coefficient) || coefficient < 0)
    throw FormatError("invalid SE coefficient");
  const float scaledMaster = coefficient * float(master);
  const float product = scaledMaster * float(command);
  const float scaled = product / 100.0f;
  const uint32_t converted = !(scaled > 0) ? 0
                             : double(scaled) >= UINT32_MAX
                                 ? UINT32_MAX
                                 : static_cast<uint32_t>(scaled);
  return {master, converted, float(converted) / 100.0f};
}
ArchiveSeResources::ArchiveSeResources(const std::filesystem::path& usrdir)
    : bank_(std::make_shared<AwbBank>(usrdir / "se.awb")) {
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
void ArchiveSeResources::Prepare(int32_t track) {
  if (track < 0) throw FormatError("negative SE track ID");
  if (!prepared_.contains(track)) {
    const auto reader = Open(track);
    prepared_.emplace(track, reader->Info());
  }
}
std::unique_ptr<HcaWaveReader> ArchiveSeResources::Open(int32_t track) const {
  if (track < 0) throw FormatError("negative SE track ID");
  return std::make_unique<HcaWaveReader>(bank_, uint32_t(track));
}
const HcaWaveInfo& ArchiveSeResources::Info(int32_t track) const {
  return prepared_.at(track);
}
void StartPlaySe(SemanticState& state, const PlaySeCommand& command,
                 SeResources& resources) {
  resources.Prepare(command.Track);  // Validate actual media before mutation.
  SePlayRequest request;
  request.Track = command.Track;
  request.Channel = std::bit_cast<int32_t>(uint32_t(command.Channel) + 2u);
  request.CommandVolume = command.Volume;
  request.Remember = command.Remember;
  if (state.SeMasterVolume)
    request.Volume = SeVolume(*state.SeMasterVolume, command.Volume,
                              resources.Coefficient());
  if (command.Remember) {
    state.SeTrack = command.Track;
    state.SeCommandVolume = command.Volume;
    state.SeChannel = command.Channel;
  }
  state.PendingEvents.push_back(
      {GameEventKind::PlaySe, command.Track, {}, command.Volume, {}, request});
}
}  // namespace Impacto::Saekano
