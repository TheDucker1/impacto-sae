#pragma once
#include "gauge.h"
#include <optional>

namespace Impacto::Saekano {
struct ScriptMovieCommand {
  std::u16string Name;
  uint32_t AllowInputSkip = 0;
  static ScriptMovieCommand Decode(std::span<const uint8_t> payload);
};
// Codec/output are existing services, not timers supplied by this controller.
// RetireFinished is a mutating release poll, distinct from decoder/device EOF.
class ScriptMovieServices {
 public:
  virtual ~ScriptMovieServices() = default;
  virtual void StopSharedAudio() = 0;
  virtual void Prepare(std::u16string_view name) = 0;  // channel0, no loop
  virtual void SetVolume(int32_t percent) = 0;
  virtual bool Ready() const = 0;
  virtual void Start() = 0;
  virtual bool Busy() const = 0;
  virtual void Stop() = 0;
  virtual bool RetireFinished() = 0;
};
struct ScriptMovieState {
  ScriptMovieCommand Command;
  bool Started = false, PlaybackStarted = false, Completed = false;
  bool MovieActive = false, TweenInitialized = false;
  int32_t Phase = 0, ScreenFade = 0, SkipPhase = 0;
  std::optional<int32_t> PendingPhase;
  ScriptLinearTween Tween;
};
// currentFade preserves C1504: native Start does not assign it. User volume is
// the native unsigned option byte; conversion to percent is option*10.
void StartScriptMovie(ScriptMovieState& state, const ScriptMovieCommand& command,
                      ScriptMovieServices& services, uint8_t movieVolume,
                      int32_t currentFade);
// Shared native normal/skipping processor; skipping does not force completion.
bool UpdateScriptMovie(ScriptMovieState& state, ScriptMovieServices& services,
                       int32_t fadeDuration, uint8_t movieVolume, bool accept,
                       uint64_t now);
}
