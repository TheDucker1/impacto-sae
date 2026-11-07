#pragma once

#include "music.h"

namespace Impacto::Saekano {
// Profile lifecycle consumes real voice state. A PC frontend owns the audio
// device/update loop; headless decodes PCM on its deterministic clock.
class ProfileVoiceOutput {
 public:
  virtual ~ProfileVoiceOutput() = default;
  virtual void Reset() = 0;
  virtual void Prepare(Bytes encoded) = 0;
  virtual void Start(uint64_t now) = 0;
  virtual void Stop() = 0;
  virtual bool Stopped() const = 0;
  virtual void Update(uint64_t now) = 0;
  virtual std::optional<uint64_t> FramesConsumed() const {
    return std::nullopt;
  }
};
class ClockedProfileVoice final : public ProfileVoiceOutput {
 public:
  void Reset() override;
  void Prepare(Bytes encoded) override;
  void Prepare(std::unique_ptr<HcaWaveReader> reader);
  void Start(uint64_t now) override;
  void Stop() override;
  bool Stopped() const override;
  void Update(uint64_t now) override;
  std::optional<uint64_t> FramesConsumed() const override;

 private:
  std::unique_ptr<HcaWaveReader> voice_;
  std::unique_ptr<HcaWaveReader> preparedVoice_;
  BgmStopFade stopFade_;
  uint64_t epoch_ = 0, now_ = 0;
  bool playing_ = false, prepared_ = false;
};
}  // namespace Impacto::Saekano
