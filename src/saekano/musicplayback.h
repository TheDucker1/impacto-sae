#pragma once

#include "commands.h"

namespace Impacto::Saekano {
// Saekano's audio host uses Impacto's existing OpenAL channel and the same
// streaming HCA reader as headless verification. One host owns the AL context.
class MusicPlayback {
 public:
  explicit MusicPlayback(ArchiveBgmResources& resources, bool loopback = false);
  ~MusicPlayback();
  void Submit(const GameEvent& event);
  void Update(float seconds);
  void Render(std::span<int16_t> stereoPcm);  // Offline 48kHz loopback only.
  bool Playing() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Bounded integration session: shared VM PlayBgm, timed wait, zero-fade
// StopBgm. Capture uses an actual software output device and writes WAV +
// diagnostics.
int RunMusicSession(const std::filesystem::path& usrdir, uint32_t track,
                    const std::filesystem::path& captureDirectory = {});
}  // namespace Impacto::Saekano
