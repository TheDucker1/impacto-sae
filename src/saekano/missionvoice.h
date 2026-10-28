#pragma once
#include "approachmission.h"
#include "profilevoiceoutput.h"
#include <set>

namespace Impacto::Saekano {
// Separate logical channel11; never borrows ordinary dialogue channels7/8.
// Original AWB/HCA preparation with an explicit CPU playback clock.
class ClockedMissionVoice final : public ApproachMissionAudio {
 public:
  explicit ClockedMissionVoice(const std::filesystem::path& usrdir);
  void Register(std::span<const int32_t> ids) override;
  void Begin(int32_t id,uint64_t now) override;
  void Update(uint64_t now) override { voice_.Update(now); }
  void Disable() override { voice_.Reset();id_=-1; }
  void Reset() override { Disable();catalog_.clear(); }
  int32_t CurrentId() const { return id_; }
  std::optional<uint64_t> FramesConsumed() const { return voice_.FramesConsumed(); }
 private:
  std::shared_ptr<AwbBank> bank_;
  std::set<int32_t> catalog_;
  ClockedProfileVoice voice_;
  int32_t id_=-1;
};
}
