#pragma once

#include "commands.h"

namespace Impacto::Saekano {
// Saekano's audio host uses Impacto's existing OpenAL channel and the same
// streaming HCA reader as headless verification. One host owns the AL context.
class MusicPlayback {
 public:
  explicit MusicPlayback(ArchiveBgmResources& resources, bool loopback = false,
                         ArchiveSeResources* sounds = nullptr);
  ~MusicPlayback();
  void Submit(const GameEvent& event);
  void Update(float seconds);
  void Render(std::span<int16_t> stereoPcm);  // Offline 48kHz loopback only.
  bool Playing() const;
  bool SoundPlaying(size_t logicalChannel) const;
  // Native profile channel7, mapped onto an existing Impacto voice channel.
  // Caller supplies known effective gain. Preparation parses/owns HCA bytes;
  // no playback or successful missing-channel stop is fabricated.
  void PrepareProfileVoice(Bytes encoded, float gain);
  void StartProfileVoice();
  void StopProfileVoice();
  bool ProfileVoiceStopped() const;
  std::optional<float> ProfileVoicePosition() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Bounded owning-VM integration: BGM/wait/stop, or two overlapping SE channels
// with shared gain and independent stops. Capture writes actual software-device
// WAV output and diagnostics. soundChannel selects the SE sequence.
int RunMusicSession(const std::filesystem::path& usrdir, uint32_t track,
                    const std::filesystem::path& captureDirectory = {},
                    std::optional<uint32_t> soundChannel = std::nullopt);
int RunProfileVoiceSession(const std::filesystem::path& usrdir,
                           uint32_t character, uint32_t entry,
                           const std::filesystem::path& captureDirectory = {});
}  // namespace Impacto::Saekano
