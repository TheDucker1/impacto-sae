#pragma once
#include "formats.h"
#include <filesystem>
#include <functional>
#include <memory>
struct AVFrame;
namespace Impacto::Saekano {
struct MoviePlaybackState {
  bool Prepared=false,Started=false,Finished=false,VideoEof=false,AudioEof=false,Paused=false;
  uint64_t VideoDecoded=0,VideoPresented=0,AudioDecoded=0,AudioQueued=0,AudioPlayed=0;
  uint64_t PendingAudio=0,PendingVideo=0,MaxPendingAudio=0,MaxPendingVideo=0;
  int64_t PositionUs=0,FramePtsUs=-1,AudioLimit=-1;
  int32_t Volume=100;
};
// Existing Impacto decoder queues, with an output source in the caller's current
// OpenAL context. The caller supplies GL presentation and owns the context lifetime.
class MoviePlayback {
 public:
  MoviePlayback();~MoviePlayback();
  std::function<void(const AVFrame*,int64_t)> Present;
  void Prepare(const std::filesystem::path& path);
  void Update(uint64_t now);
  bool Ready()const;
  void Start(uint64_t now);
  void SetVolume(int32_t percent);
  // Pause preserves the output position and presented frame. Restart prepares
  // the same source from zero; readiness/Start remain explicit asynchronous gates.
  void SetPaused(bool paused,uint64_t now);
  void Restart();
  void Stop();
  const MoviePlaybackState& State()const;
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
