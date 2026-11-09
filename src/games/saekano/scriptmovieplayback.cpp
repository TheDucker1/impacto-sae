#include "scriptmovieplayback.h"

#include "../../profile/games/saekano/game.h"
#include "musicplayback.h"
#include "scenemessage.h"

namespace Impacto::Saekano {
PlaybackScriptMovie::PlaybackScriptMovie(const std::filesystem::path& usrdir,
    MusicPlayback& audio, SceneMessageVoiceOwner& voices,
    std::function<void(const AVFrame*, int64_t)> present,
    std::function<bool()> retireFrames)
    : root_(std::filesystem::canonical(usrdir)), audio_(audio), voices_(voices),
      retireFrames_(std::move(retireFrames)) {
  if (!present || !retireFrames_)
    throw FormatError("scenario movie requires frame presentation and retirement services");
  movie_.Present = std::move(present);
}
void PlaybackScriptMovie::Pump(uint64_t now) {
  if (now_ && now < *now_) throw FormatError("scenario movie clock moved backwards");
  now_ = now;
  movie_.Update(now);
}
void PlaybackScriptMovie::StopSharedAudio() {
  audio_.StopAllChannels();
  InvalidateSceneVoicePreparations(voices_);
}
void PlaybackScriptMovie::Prepare(std::u16string_view name) {
  if (name.empty() || name == u"." || name == u".." ||
      name.back() == u'.' || name.back() == u' ')
    throw FormatError("invalid scenario movie stem");
  for (const auto c : name)
    if (c < 32 || std::u16string_view(u"/\\:<>\"|?*").find(c) != std::u16string_view::npos)
      throw FormatError("scenario movie stem is not a file name");
  const auto path = Profile::Saekano::MoviePath(
      root_, std::u16string(name) + u".usm");
  if (!retired_)
    throw FormatError("scenario movie replacement before retirement");
  retired_ = false;
  try { movie_.Prepare(path); }
  catch (...) {
    movie_.Stop(); retired_ = retireFrames_(); throw;
  }
}
void PlaybackScriptMovie::SetVolume(int32_t percent) { movie_.SetVolume(percent); }
bool PlaybackScriptMovie::Ready() const { return !retired_ && movie_.Ready(); }
void PlaybackScriptMovie::Start() {
  if (!now_) throw FormatError("scenario movie start before host pump");
  movie_.Start(*now_);
}
bool PlaybackScriptMovie::Busy() const {
  const auto& state = movie_.State();
  return state.Prepared && !state.Finished;
}
void PlaybackScriptMovie::Stop() { movie_.Stop(); }
bool PlaybackScriptMovie::RetireFinished() {
  if (Busy()) return false;
  if (retired_) return true;
  // Decoder EOF alone is insufficient. Stop joins the worker, deletes its AL
  // source/buffers, frees decoded queues, and then the frontend retires frames.
  movie_.Stop();
  if (!retireFrames_()) return false;
  const auto& state = movie_.State();
  retired_ = !state.Prepared && !state.Started && !state.PendingAudio && !state.PendingVideo;
  return retired_;
}
}
