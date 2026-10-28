#include "missionvoice.h"
#include "commands.h"

namespace Impacto::Saekano {
ClockedMissionVoice::ClockedMissionVoice(const std::filesystem::path& usrdir)
    : bank_(std::make_shared<AwbBank>(usrdir/"voice.awb")) {}
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
  if(!catalog_.contains(id))throw UnsupportedCommand("mission voice was not registered at script load");
  auto reader=std::make_unique<HcaWaveReader>(bank_,uint32_t(id));
  voice_.Prepare(std::move(reader));voice_.Start(now);id_=id;
}
}
