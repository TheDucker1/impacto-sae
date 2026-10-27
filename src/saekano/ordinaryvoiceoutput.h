#pragma once

#include "ordinaryvoice.h"
#include "profilevoiceoutput.h"

namespace Impacto::Saekano {
// Two actual HCA decoders on an explicit headless clock. Reuses the existing
// profile PCM consumer; readiness and repeated-start semantics belong here.
class ClockedOrdinaryVoice final : public OrdinaryVoiceOutput {
 public:
  explicit ClockedOrdinaryVoice(const std::filesystem::path& usrdir);
  void Prepare(uint32_t channel, int16_t voiceId) override;
  void Start(uint32_t channel) override;
  void Stop(uint32_t channel) override;
  void Update(uint64_t now) override;
  bool Ready(uint32_t channel) const override;
  bool Playing(uint32_t channel) const override;
  int32_t Position(uint32_t channel) const override;
  std::optional<uint64_t> FramesConsumed(uint32_t channel) const;
 private:
  std::shared_ptr<AwbBank> bank_;
  std::array<ClockedProfileVoice, 2> voices_;
  std::array<bool, 2> ready_{};
  std::array<uint32_t, 2> rates_{};
  uint64_t now_ = 0;
  bool sampled_ = false;
};
}  // namespace Impacto::Saekano
