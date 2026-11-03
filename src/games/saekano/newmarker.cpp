#include "newmarker.h"
#include "skipauto.h"
#include "commands.h"
#include <bit>

namespace Impacto::Saekano {
NewMarkerEnvironment NewMarkerEnvironment::Decode(std::span<const uint8_t> bytes){
  const auto prefix=SkipAutoEnvironment::Decode(bytes).PrefixBytes;
  PayloadReader r(bytes.subspan(prefix));NewMarkerEnvironment e;
  for(auto& v:e.Base)v=r.I32();
  const auto count=r.U32();r.CheckCount(count,24);
  if(count<4)throw FormatError("new marker needs four particle descriptors");
  e.Particles.resize(count);
  for(auto& p:e.Particles)for(auto& v:p)v=r.I32();
  for(auto& v:e.Scale)v=r.I32();
  const auto duration=[&](){const auto v=r.I32();if(v<0)throw FormatError("negative new marker duration");return uint32_t(v);};
  for(auto& v:e.ScaleDuration)v=duration();
  e.SpawnDuration=duration();e.CycleDuration=duration();
  e.PrefixBytes=bytes.size()-r.Remaining();return e;
}
namespace {
void Commit(int32_t& phase,std::optional<int32_t>& pending){
  if(pending){phase=*pending;pending.reset();}
}
void Reset(NewMarkerParticle& p,const NewMarkerEnvironment& e,uint64_t now){
  p.Phase=0;p.PendingPhase.reset();
  p.Scale.Configure(1,e.Scale[0],e.ScaleDuration[0],false,now);
}
void Tick(NewMarkerParticle& p,const NewMarkerEnvironment& e,uint64_t now){
  Commit(p.Phase,p.PendingPhase);
  if(p.Phase==0){p.Scale.Restart(now);p.PendingPhase=1;}
  if(p.Phase==1){p.Scale.Tick(now);if(p.Scale.Finished()){
    p.Scale.Configure(e.Scale[0],e.Scale[1],e.ScaleDuration[1],true,now);p.PendingPhase=2;}}
  if(p.Phase==2){p.Scale.Tick(now);if(p.Scale.Finished()){
    p.Scale.Configure(e.Scale[1],e.Scale[2],e.ScaleDuration[2],true,now);p.PendingPhase=3;}}
  if(p.Phase==3){p.Scale.Tick(now);if(p.Scale.Finished())p.PendingPhase=4;}
}
}
NewMarkerState StartNewMarker(const NewMarkerEnvironment& e,uint64_t now){
  NewMarkerState s;
  for(auto& p:s.Particles)p.Scale.Configure(1,e.Scale[0],e.ScaleDuration[0],false,now);
  s.Cycle.Configure(0,100,e.CycleDuration,false,now);
  s.Spawn.Configure(0,100,e.SpawnDuration,false,now);return s;
}
void UpdateNewMarker(NewMarkerState& s,const NewMarkerEnvironment& e,uint64_t now){
  Commit(s.Phase,s.PendingPhase);
  if(s.Phase==0){s.Cycle.Restart(now);s.Spawn.Restart(now);Reset(s.Particles[0],e,now);s.PendingPhase=1;}
  if(s.Phase==1){
    s.Cycle.Tick(now);s.Spawn.Tick(now);
    for(auto& p:s.Particles)Tick(p,e,now);
    if(s.Spawn.Finished()){
      // Native continues counting after all four particles have been launched.
      s.Index=std::bit_cast<int32_t>(uint32_t(s.Index)+1);
      if(s.Index>=0 && s.Index<4)Reset(s.Particles[size_t(s.Index)],e,now);
      s.Spawn.Restart(now);
    }
    if(s.Cycle.Finished()){s.Index=0;s.PendingPhase=0;}
  }
}
std::array<UiSpriteBlit,5> BuildNewMarker(const NewMarkerState& s,
    const NewMarkerEnvironment& e,ProfileTextureKey texture,std::array<int32_t,2> anchor){
  if(e.Particles.size()<4)throw FormatError("missing new marker particles");
  const auto sprite=[&](const std::array<int32_t,6>& p,int32_t alignment,float scale){
    const auto add=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
    UiSpriteDefinition d;d.Alignment=alignment;d.X=add(anchor[0],p[0]);d.Y=add(anchor[1],p[1]);
    d.SourceX=p[2];d.SourceY=p[3];d.Width=p[4];d.Height=p[5];d.Color={255,255,255,255};
    return ResolveUiSprite(texture,d,scale,scale);
  };
  std::array<UiSpriteBlit,5> out;out[0]=sprite(e.Base,0,1);
  for(size_t i=0;i<4;++i)out[i+1]=sprite(e.Particles[i],4,float(s.Particles[i].Scale.Value.Current)/10.0f);
  return out;
}
}
