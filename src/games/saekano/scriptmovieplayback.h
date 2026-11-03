#pragma once

#include "movieplayback.h"
#include "scriptmovie.h"

namespace Impacto::Saekano {
class MusicPlayback;
struct SceneMessageVoiceOwner;

// Physical scenario-movie service adapter. Reuses the existing decoder/device
// owner; the frontend owns frame storage and must confirm its actual release.
// Audio, dialogue owner and frame callbacks must outlive this adapter. Pump at
// the host clock before the controller update; Start uses that sampled clock.
class PlaybackScriptMovie final : public ScriptMovieServices {
 public:
  PlaybackScriptMovie(const std::filesystem::path& usrdir, MusicPlayback& audio,
      SceneMessageVoiceOwner& voices,
      std::function<void(const AVFrame*, int64_t)> present,
      std::function<bool()> retireFrames);
  void Pump(uint64_t now);
  void StopSharedAudio() override;
  void Prepare(std::u16string_view name) override;
  void SetVolume(int32_t percent) override;
  bool Ready() const override;
  void Start() override;
  bool Busy() const override;
  void Stop() override;
  bool RetireFinished() override;
  const MoviePlaybackState& PlaybackState() const { return movie_.State(); }

 private:
  std::filesystem::path root_;
  MusicPlayback& audio_;
  SceneMessageVoiceOwner& voices_;
  std::function<bool()> retireFrames_;
  MoviePlayback movie_;
  std::optional<uint64_t> now_;
  bool retired_ = true;
};
}
