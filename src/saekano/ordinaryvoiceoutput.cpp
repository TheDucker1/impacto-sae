#include "ordinaryvoiceoutput.h"
#include "commands.h"

namespace Impacto::Saekano {
namespace {
size_t Slot(uint32_t channel) {
  if (channel<7 || channel>10)
    throw UnsupportedCommand("clocked ordinary voice channel outside7..10");
  return size_t(channel - 7);
}
}  // namespace
ClockedOrdinaryVoice::ClockedOrdinaryVoice(const std::filesystem::path& usrdir)
    : bank_(std::make_shared<AwbBank>(usrdir / "voice.awb")) {}
void ClockedOrdinaryVoice::Prepare(uint32_t channel, int16_t voiceId) {
  const auto slot = Slot(channel);
  if (voiceId < 0) throw FormatError("negative ordinary voice ID");
  auto reader = std::make_unique<HcaWaveReader>(bank_, uint32_t(voiceId));
  const auto rate = reader->Info().SampleRate;
  voices_[slot].Prepare(std::move(reader));
  rates_[slot] = rate;
  ready_[slot] = true;
}
void ClockedOrdinaryVoice::Start(uint32_t channel) {
  const auto slot = Slot(channel);
  if (!ready_[slot]) return;
  if (!sampled_) throw UnsupportedCommand("ordinary voice clock is unknown");
  voices_[slot].Start(now_);
  ready_[slot] = false;
}
void ClockedOrdinaryVoice::Stop(uint32_t channel) {
  const auto slot = Slot(channel);
  voices_[slot].Reset();
  ready_[slot] = false;
  rates_[slot] = 0;
}
void ClockedOrdinaryVoice::Update(uint64_t now) {
  if (sampled_ && now < now_) throw FormatError("ordinary voice clock moved backwards");
  for (auto& voice : voices_) voice.Update(now);
  now_ = now;
  sampled_ = true;
}
bool ClockedOrdinaryVoice::Ready(uint32_t channel) const { return ready_[Slot(channel)]; }
bool ClockedOrdinaryVoice::Playing(uint32_t channel) const {
  const auto slot = Slot(channel);
  return !ready_[slot] && voices_[slot].FramesConsumed().has_value() && !voices_[slot].Stopped();
}
int32_t ClockedOrdinaryVoice::Position(uint32_t channel) const {
  const auto slot = Slot(channel);
  if (!Playing(channel)) return -1;
  const auto position = *voices_[slot].FramesConsumed() * 1000 / rates_[slot];
  if (position > INT32_MAX) throw FormatError("ordinary voice exceeds position range");
  return int32_t(position);
}
std::optional<uint64_t> ClockedOrdinaryVoice::FramesConsumed(uint32_t channel) const {
  return voices_[Slot(channel)].FramesConsumed();
}
}  // namespace Impacto::Saekano
