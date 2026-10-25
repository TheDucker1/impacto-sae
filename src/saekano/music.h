#pragma once

#include "audio.h"

#include <array>
#include <map>
#include <string>
#include <variant>

namespace Impacto::Saekano {
struct SemanticState;
// Explicit fresh-owner operation, before loading persistent/global progress.
// Existing or partial audio inputs reject; VM replacement never calls this.
void InitializeFreshAudioOwner(SemanticState& state);

struct AudioEnvironment {
  std::array<uint8_t, 3> Flags{};
  int32_t OpaqueWord = 0;
  std::vector<int32_t> OpaqueTable;
  uint8_t OpaqueFlag = 0;
  uint32_t PackedColor = 0;
  int32_t OpaqueTail = 0;
  float BgmCoefficient = 0;
  float SeCoefficient = 0;
  static AudioEnvironment Decode(std::span<const uint8_t> bytes);
};

enum class MusicStatusSet { AnyI32, OneTwoOrThree };
using MusicHistoryEntry = std::variant<int32_t, MusicStatusSet>;
struct MusicHistoryState {
  bool ExtentKnown = false;
  std::vector<MusicHistoryEntry> Entries;
  int32_t Default = 0;
  bool AllKnown() const;
};
int32_t ExactMusicStatus(const MusicHistoryState& history, size_t id);
void MarkMusicPlayed(MusicHistoryState& history, size_t id);
enum class MusicExecutionPolicy { Concrete, PartialNativeInputs };

struct BgmVolume {
  int32_t MasterVolume = 0;
  uint32_t ConvertedVolume = 0;
  float Gain = 0;
};
struct BgmPlayRequest {
  int32_t Track = 0;
  int32_t CommandVolume = 0;
  std::optional<BgmVolume> Volume;
};

struct PlayBgmCommand {
  std::u16string TrackText;
  int32_t Volume = 0;
  static PlayBgmCommand Decode(std::span<const uint8_t> bytes);
};
int32_t ParseBgmTrack(std::u16string_view text);
BgmPlayRequest BgmRequest(int32_t track, int32_t master, int32_t command,
                          float coefficient);

class BgmResources {
 public:
  virtual ~BgmResources() = default;
  virtual void Prepare(int32_t track) = 0;
  virtual float Coefficient() const = 0;
};

// Validate and actually initialize the selected original HCA decoder before a
// VM can submit playback. The output consumer can independently open a reader.
class ArchiveBgmResources final : public BgmResources {
 public:
  explicit ArchiveBgmResources(const std::filesystem::path& usrdir);
  void Prepare(int32_t track) override;
  float Coefficient() const override { return environment_.BgmCoefficient; }
  const HcaWaveInfo& Info(int32_t track) const;
  std::unique_ptr<HcaWaveReader> Open(int32_t track) const;

 private:
  AudioEnvironment environment_;
  std::shared_ptr<AwbBank> bank_;
  std::map<int32_t, HcaWaveInfo> prepared_;
};

void StartPlayBgm(SemanticState& state, const PlayBgmCommand& command,
                  BgmResources& resources);
}  // namespace Impacto::Saekano
