#pragma once
#include "approachmission.h"
#include "ordinaryvoiceoutput.h"
#include <set>

namespace Impacto::Saekano {
// Channel11 shares the real pool with battle preview11; dialogue7..10 and
// preview12 remain independent. A standalone owner allocates its own pool.
class ClockedMissionVoice final : public ApproachMissionAudio {
 public:
  explicit ClockedMissionVoice(const std::filesystem::path& usrdir);
  ClockedMissionVoice(const std::filesystem::path& usrdir,ClockedOrdinaryVoice& shared);
  void Register(std::span<const int32_t> ids) override;
  void Begin(int32_t id,uint64_t now) override;
  void Prepare(int32_t id) override;
  void Start(uint64_t now) override;
  void Update(uint64_t now) override { output_->Update(now); }
  void Disable() override { output_->Stop(11); }
  void Reset() override { Disable();catalog_.clear(); }
  int32_t CurrentId() const { return output_->VoiceId(11).value_or(-1); }
  std::optional<uint64_t> FramesConsumed() const { return output_->FramesConsumed(11); }
  bool Ready() const { return output_->Ready(11); }
  bool Playing() const { return output_->Playing(11); }
 private:
  std::shared_ptr<AwbBank> bank_;
  std::set<int32_t> catalog_;
  std::unique_ptr<ClockedOrdinaryVoice> owned_;
  ClockedOrdinaryVoice* output_;
};
}
