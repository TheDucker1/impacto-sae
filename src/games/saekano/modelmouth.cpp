#include "modelmouth.h"
#include "commands.h"

namespace Impacto::Saekano {
void ModelMouthEnvelope::Begin(const ProfileMouthAnimation& envelope,uint64_t now) {
  if(envelope.Samples.size()>1024*1024)throw FormatError("model mouth samples exceed budget");
  Samples=envelope.Samples;Epoch=now;
  // Native Begin replaces samples/epoch, preserving active interval state.
}
void ModelMouthEnvelope::Stop() { Samples.clear(); }
void ModelMouthEnvelope::Update(uint64_t now) {
  const auto index=uint32_t((now-Epoch)/85);
  const bool sample=index<Samples.size() && Samples[index]!=0;
  if(Active && sample)Elapsed=now-ActiveEpoch;
  else if(!Active && sample){ActiveEpoch=now;Elapsed=0;Active=true;}
  else if(Active)Active=false;
}
}
