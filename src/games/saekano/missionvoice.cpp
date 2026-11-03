#include "missionvoice.h"
#include "commands.h"

namespace Impacto::Saekano {
ClockedMissionVoice::ClockedMissionVoice(const std::filesystem::path& usrdir)
    : bank_(std::make_shared<AwbBank>(usrdir/"voice.awb")),owned_(std::make_unique<ClockedOrdinaryVoice>(usrdir)),output_(owned_.get()) {}
ClockedMissionVoice::ClockedMissionVoice(const std::filesystem::path& usrdir,ClockedOrdinaryVoice& shared)
    : bank_(std::make_shared<AwbBank>(usrdir/"voice.awb")),output_(&shared) {}
void ClockedMissionVoice::Register(std::span<const int32_t> ids) {
  std::set<int32_t> prepared;
  for(const auto id:ids) {
    if(id<0)continue;
    if(id>INT16_MAX)throw FormatError("mission voice ID exceeds verified signed16 range");
    (void)bank_->Index().Find(uint32_t(id));prepared.insert(id);
  }
  catalog_=std::move(prepared);
}
void ClockedMissionVoice::Begin(int32_t id,uint64_t now) {
  Prepare(id);Start(now);
}
void ClockedMissionVoice::Prepare(int32_t id) {
  if(!catalog_.contains(id))throw UnsupportedCommand("mission voice was not registered at script load");
  output_->Prepare(11,int16_t(id));
}
void ClockedMissionVoice::Start(uint64_t now) {
  if(!Ready())return; // Native Start on an absent/disabled channel is a no-op.
  output_->Update(now);output_->Start(11);
}
}
