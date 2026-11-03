#include "titleeffect.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
int32_t Setting(const TitleEnvironment& e,uint16_t at){return std::bit_cast<int32_t>(e.Scalars.at(at));}
bool Active(const TitleEffectParticle& p){return p.Position[0]>-500 || p.Position[1]>-500;}
void Validate(const TitleEnvironment& e){
  if(e.ColorTables.at(0x174).empty() || e.IntegerTables.at(0x184).size()<2)throw FormatError("title effect has no color/opacity sequence");
}
void Configure(TitleEffectState& s,const TitleEnvironment& e,uint64_t now){
  Validate(e);
  for(auto& p:s.Particles){
    p.Position={-500,-500};p.Shrinking=false;p.Sequence=0;
    p.Alpha.Start(255,0,Setting(e,0x14c),now);p.NextAlpha.Start(255,0,Setting(e,0x14c),now);
    p.Scale.Start(0,Setting(e,0x154),Setting(e,0x150),now);
  }
  s.Spawn.Start(0,100,s.Random.Range(0,Setting(e,0x148)),now);s.Phase=0;s.PendingPhase.reset();
}
void SpawnTimer(TitleEffectState& s,const TitleEnvironment& e,uint64_t now){s.Spawn.Start(0,100,s.Random.Range(0,Setting(e,0x148)),now);}
int32_t WrapAdd(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t Truncate(float x){if(std::isnan(x))return 0;if(x>=2147483648.0f)return INT32_MAX;if(x<=-2147483648.0f)return INT32_MIN;return int32_t(x);}
}
void StartTitleEffects(TitleEffectState& s,const TitleEnvironment& e,uint64_t now){s={};s.Started=true;s.Random.Seed(uint32_t(now));Configure(s,e,now);}
void ResetTitleEffects(TitleEffectState& s,const TitleEnvironment& e,int32_t variant,uint64_t now){
  if(!s.Started || variant<1 || variant>3)throw FormatError("title effect has no active variant owner");s.Variant=variant;Configure(s,e,now);
}
void UpdateTitleEffects(TitleEffectState& s,const TitleEnvironment& e,uint64_t now){
  if(!s.Started)throw FormatError("title effect is not constructed");Validate(e);
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  if(s.Phase==0)s.PendingPhase=s.Variant;
  if(s.Phase<1 || s.Phase>3)return;
  s.Spawn.Tick(now);
  if(s.Spawn.Finished()){
    auto slot=std::find_if(s.Particles.begin(),s.Particles.end(),[](const auto& p){return !Active(p);});
    if(slot!=s.Particles.end()){
      auto& p=*slot;
      if(s.Phase==2){
        p.Scale.Start(s.Random.Range(Setting(e,0x15c),Setting(e,0x160)),100,Setting(e,0x150),now);
        const auto product=std::bit_cast<int32_t>(uint32_t(e.Sprites.at(0x100).Width)*uint32_t(p.Scale.Current));
        const auto half=Truncate(float(product)/100.0f);
        p.Position[0]=float(s.Random.Range(480,WrapAdd(half,1060)));
        if(p.Position[0]>float(WrapAdd(half,960)))p.Position[1]=float(s.Random.Range(272,WrapAdd(half,644)));
        else p.Position[1]=float(WrapAdd(WrapAdd(half,s.Random.Range(0,100)),544));
      }else{p.Position[0]=float(s.Random.Range(0,960));p.Position[1]=float(s.Random.Range(0,544));}
      p.ColorIndex=uint32_t(s.Random.Range(0,int32_t(e.ColorTables.at(0x174).size())));
      if(s.Phase==3){
        const auto& levels=e.IntegerTables.at(0x184);
        p.Alpha.Start(levels[0],0,Setting(e,0x14c),now);p.NextAlpha.Start(0,levels[1],Setting(e,0x14c),now);
        p.Scale.Start(100,Setting(e,0x154),Setting(e,0x150),now);
        p.Hold.Start(0,100,s.Random.Range(Setting(e,0x16c),Setting(e,0x170)),now);
      }else{
        p.Alpha.Start(255,0,Setting(e,0x14c),now);
        if(s.Phase==1)p.Scale.Start(0,Setting(e,0x154),Setting(e,0x150),now);
      }
    }
    SpawnTimer(s,e,now);
  }
  for(auto& p:s.Particles){
    if(!Active(p))continue;
    if(s.Phase==1){
      p.Scale.Tick(now);
      if(p.Scale.Finished() && !p.Shrinking){p.Shrinking=true;p.Scale.Start(p.Scale.To,Setting(e,0x158),Setting(e,0x150),now);}
      if(p.Shrinking){p.Alpha.Tick(now);if(p.Alpha.Finished()){p.Shrinking=false;p.Position={-500,-500};}}
    }else if(s.Phase==2){
      const auto dx=s.Random.Range(Setting(e,0x164),Setting(e,0x168));
      const auto dy=s.Random.Range(Setting(e,0x164),Setting(e,0x168));
      p.Position[0]-=float(dx);p.Position[1]-=float(dy);
    }else{
      p.Hold.Tick(now);
      if(p.Hold.Finished()){
        p.Alpha.Tick(now);p.NextAlpha.Tick(now);
        if(p.Alpha.Finished()){
          const auto& levels=e.IntegerTables.at(0x184);
          p.Sequence=(p.Sequence+1)%int32_t(levels.size());
          p.Alpha.Start(levels[size_t(p.Sequence)],0,Setting(e,0x14c),now);
          const auto next=(size_t(p.Sequence)+1)%levels.size();
          p.NextAlpha.Start(0,levels[next],Setting(e,0x14c),now);
          p.Hold.StartUnsigned(p.Hold.From,p.Hold.To,p.Hold.Duration,now);
          if(p.Sequence==0)p.Position={-500,-500};
        }
      }
    }
  }
}
std::vector<UiSpriteBlit> BuildTitleEffects(const TitleEffectState& s,const TitleEnvironment& e){
  if(!s.Started)throw FormatError("title effect is not constructed");Validate(e);std::vector<UiSpriteBlit> out;const auto& sprite=e.Sprites.at(0x100);
  const auto emit=[&](const TitleEffectParticle& p,int32_t alpha,bool fixed){
    float scale=fixed?1.0f:float(p.Scale.Current)/100.0f;
    auto sw=sprite.Width,sh=sprite.Height;
    if(!fixed && !(scale>0)){sw=std::bit_cast<int32_t>(0u-uint32_t(sw));sh=std::bit_cast<int32_t>(0u-uint32_t(sh));scale=-scale;}
    const auto width=Truncate(float(sprite.Width)*scale),height=Truncate(float(sprite.Height)*scale);
    auto color=e.ColorTables.at(0x174).at(p.ColorIndex);color[3]=uint8_t(alpha);
    out.push_back({{ProfileTextureSection::TitleUi,-1,68},{WrapAdd(Truncate(p.Position[0]),-(width/2)),WrapAdd(Truncate(p.Position[1]),-(height/2)),width,height},
      {sprite.SourceX,sprite.SourceY,sw,sh},color});
  };
  for(const auto& p:s.Particles)if(Active(p)){
    emit(p,p.Alpha.Current,s.Phase==3);
    // Native draw peeks forward in the circular sequence, draws the second
    // layer unless that peek wraps to the first entry, then restores the index.
    if(s.Phase==3 && (size_t(p.Sequence)+1)%e.IntegerTables.at(0x184).size()!=0)emit(p,p.NextAlpha.Current,true);
  }
  return out;
}
}
