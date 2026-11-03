#pragma once
#include "profile.h"

namespace Impacto::Saekano {
// Native model+5c0 control, separate from the SDK mouth deformation/triangle.
struct ModelMouthEnvelope {
  uint64_t Epoch=0,ActiveEpoch=0,Elapsed=0;
  bool Active=false;
  std::vector<uint8_t> Samples;
  void Begin(const ProfileMouthAnimation& envelope,uint64_t now);
  void Stop();
  void Update(uint64_t now);
};
class OrdinaryModelMouthServices {
 public:
  virtual ~OrdinaryModelMouthServices()=default;
  virtual bool MouthResourceReady(int32_t resource) const=0;
  virtual void BeginMouthEnvelope(int32_t resource,const ProfileMouthAnimation& envelope,uint64_t now)=0;
  virtual void EndMouthEnvelope(int32_t resource)=0;
};
}
