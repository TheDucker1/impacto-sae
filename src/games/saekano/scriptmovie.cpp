#include "scriptmovie.h"
#include "commands.h"

namespace Impacto::Saekano {
ScriptMovieCommand ScriptMovieCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  return {reader.String(), reader.U32()};  // Native alignment tail is not a field.
}
void StartScriptMovie(ScriptMovieState& state, const ScriptMovieCommand& command,
                      ScriptMovieServices& services, uint8_t movieVolume,
                      int32_t currentFade) {
  ScriptMovieState next;
  next.Command = command;
  next.ScreenFade = currentFade;
  services.StopSharedAudio();
  services.Prepare(command.Name);
  services.SetVolume(int32_t(movieVolume) * 10);
  next.Started = next.MovieActive = true;
  state = std::move(next);
}
bool UpdateScriptMovie(ScriptMovieState& state, ScriptMovieServices& services,
                       int32_t fadeDuration, uint8_t movieVolume, bool accept,
                       uint64_t now) {
  auto& s = state;
  if (!s.Started) throw UnsupportedCommand("movie command Start was suppressed");
  if (s.Completed) return true;
  if (s.PendingPhase) { s.Phase = *s.PendingPhase; s.PendingPhase.reset(); }
  const auto configure = [&](int32_t from, int32_t to) {
    s.Tween.Start(from, to, fadeDuration, now);
    s.TweenInitialized = true;
  };
  const auto tick = [&] {
    if (!s.TweenInitialized) throw UnsupportedCommand("movie tween is uninitialized");
    s.Tween.Tick(now);
    s.ScreenFade = (s.Tween.Current * 255) / 100;
    return s.Tween.Finished();
  };
  if (s.Phase == 0) { configure(100, 0); s.Phase = 1; }
  if (s.Phase == 1 && tick()) s.PendingPhase = 2;
  if (s.Phase == 2) {
    if (!s.PlaybackStarted) {
      if (services.Ready()) { services.Start(); s.PlaybackStarted = true; }
    } else {
      if (s.Command.AllowInputSkip && accept) s.PendingPhase = 3;
      if (!services.Busy()) {
        services.Stop();
        if (services.RetireFinished()) s.PendingPhase = 6;
      }
    }
  }
  if (s.Phase == 3) { configure(0, 100); s.PendingPhase = 4; }
  if (s.Phase == 4) {
    const bool done = tick();
    services.SetVolume((int32_t(movieVolume) * 10 * (100 - s.Tween.Current)) / 100);
    if (done) { services.Stop(); s.PendingPhase = 5; }
  }
  if (s.Phase == 5 && !services.Busy()) {
    services.Stop();
    if (services.RetireFinished()) s.PendingPhase = 6;
  }
  if (s.Phase == 6) {
    s.ScreenFade = 255;
    s.MovieActive = false;
    s.Completed = true;
  }
  return s.Completed;
}
}
