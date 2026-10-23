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
}  // namespace Impacto::Saekano
