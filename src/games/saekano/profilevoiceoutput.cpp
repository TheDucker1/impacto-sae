#include "profilevoiceoutput.h"

#include "commands.h"

#include <algorithm>

namespace Impacto::Saekano {
void ClockedProfileVoice::Reset() {
  voice_.reset();
  preparedVoice_.reset();
  stopFade_ = {};
  playing_ = prepared_ = false;
  epoch_ = now_ = 0;
}
void ClockedProfileVoice::Prepare(Bytes encoded) {
  Prepare(std::make_unique<HcaWaveReader>(std::move(encoded)));
}
void ClockedProfileVoice::Prepare(std::unique_ptr<HcaWaveReader> reader) {
  if (!reader) throw FormatError("voice decoder is unavailable");
  if (voice_ || preparedVoice_) Stop();
  preparedVoice_ = std::move(reader);
  prepared_ = true;
}
void ClockedProfileVoice::Start(uint64_t now) {
  if (!prepared_ || !preparedVoice_)
    throw UnsupportedCommand(
        "profile voice must be prepared before each start");
  prepared_ = false;
  voice_ = std::move(preparedVoice_);
  stopFade_ = {};
  epoch_ = now_ = now;
  playing_ = true;
}
void ClockedProfileVoice::Stop() {
  if (!voice_ && !preparedVoice_)
    throw FormatError("profile voice has no stop acknowledgement");
  if (prepared_ || stopFade_.Active() || !playing_) {
    if (prepared_) voice_ = std::move(preparedVoice_);
    preparedVoice_.reset();
    prepared_ = playing_ = false;
    stopFade_ = {};
  } else {
    stopFade_.Request(500);
    stopFade_.Tick(0); stopFade_.Tick(0);
  }
}
bool ClockedProfileVoice::Stopped() const {
  return bool(voice_ || preparedVoice_) && !playing_;
}
std::optional<uint64_t> ClockedProfileVoice::FramesConsumed() const {
  if (preparedVoice_) return 0;
  return voice_ ? std::optional(voice_->Position()) : std::nullopt;
}
void ClockedProfileVoice::Update(uint64_t now) {
  if (now < now_) throw FormatError("profile voice clock moved backwards");
  if (stopFade_.Active()) {
    stopFade_.Tick(uint32_t(std::min<uint64_t>(now - now_, 67)));
    if (!stopFade_.Active()) playing_ = false;
  }
  now_ = now;
  if (!playing_) return;
  if (!voice_ || now < epoch_) throw FormatError("invalid profile voice clock");
  const auto& info = voice_->Info();
  const uint64_t elapsed = now - epoch_;
  const uint64_t end =
      elapsed / 1000 >= (info.Frames + info.SampleRate - 1) / info.SampleRate
          ? info.Frames
          : std::min(info.Frames, elapsed * info.SampleRate / 1000);
  std::array<int16_t, 8192> block;
  if (info.Channels == 0 || info.Channels > block.size())
    throw FormatError("profile voice channel count exceeds PCM budget");
  while (voice_->Position() < end) {
    const size_t count = size_t(std::min<uint64_t>(
        end - voice_->Position(), block.size() / info.Channels));
    if (!voice_->Read(std::span(block).first(count * info.Channels)))
      throw FormatError("profile voice ended before declared PCM length");
  }
  if (voice_->Position() == info.Frames) playing_ = false;
}
}  // namespace Impacto::Saekano
