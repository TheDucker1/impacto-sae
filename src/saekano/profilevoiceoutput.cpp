#include "profilevoiceoutput.h"

#include "commands.h"

#include <algorithm>

namespace Impacto::Saekano {
void ClockedProfileVoice::Reset() {
  voice_.reset();
  playing_ = prepared_ = false;
  epoch_ = 0;
}
void ClockedProfileVoice::Prepare(Bytes encoded) {
  Prepare(std::make_unique<HcaWaveReader>(std::move(encoded)));
}
void ClockedProfileVoice::Prepare(std::unique_ptr<HcaWaveReader> reader) {
  if (!reader) throw FormatError("voice decoder is unavailable");
  voice_ = std::move(reader);
  prepared_ = true;
  playing_ = false;
}
void ClockedProfileVoice::Start(uint64_t now) {
  if (!prepared_ || !voice_)
    throw UnsupportedCommand(
        "profile voice must be prepared before each start");
  prepared_ = false;
  epoch_ = now;
  playing_ = true;
}
void ClockedProfileVoice::Stop() {
  if (!voice_) throw FormatError("profile voice has no stop acknowledgement");
  playing_ = false;
}
bool ClockedProfileVoice::Stopped() const { return bool(voice_) && !playing_; }
std::optional<uint64_t> ClockedProfileVoice::FramesConsumed() const {
  return voice_ ? std::optional(voice_->Position()) : std::nullopt;
}
void ClockedProfileVoice::Update(uint64_t now) {
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
