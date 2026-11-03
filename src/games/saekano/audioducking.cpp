#include "audioducking.h"
#include "commands.h"
#include <algorithm>
#include <bit>
namespace Impacto::Saekano {
namespace {
int32_t Bits(uint32_t v){return std::bit_cast<int32_t>(v);}
int32_t Add(int32_t a,int32_t b){return Bits(uint32_t(a)+uint32_t(b));}
int32_t Sub(int32_t a,int32_t b){return Bits(uint32_t(a)-uint32_t(b));}
int32_t Mul(int32_t a,int32_t b){return Bits(uint32_t(a)*uint32_t(b));}
void Check(int32_t percent){if(percent<0 || percent>100)throw FormatError("audio attenuation percentage outside0..100");}
}
void BgmVoiceAttenuation::SetPercent(int32_t percent){Check(percent);Percent=percent;}
void BgmVoiceAttenuation::SetGain(int32_t percent,bool available){
  Check(percent);if(available)SavedGain=CurrentGain=percent;
}
void BgmVoiceAttenuation::StartVoice(uint32_t channel,bool ready,uint64_t now,bool available){
  if(channel>12)throw FormatError("attenuation start outside native channels");
  if(!ready || !Enabled || channel<7)return;
  if(available){Delta=Base=CurrentGain;Epoch=now;Phase=1;}
  Started=true;
}
std::optional<int32_t> BgmVoiceAttenuation::Tick(uint64_t now,AudioDuckingInputs in){
  std::optional<int32_t> publication;
  if(Started){
    if(Phase>0){
      const auto elapsed=std::min(Bits(uint32_t(now-Epoch)),300);
      const auto target=Phase==1?Mul(SavedGain,Percent)/100:SavedGain;
      Delta=Mul(Sub(target,Base),elapsed)/300;
      const auto value=Add(Base,Delta);
      if(in.TargetAvailable){CurrentGain=value;publication=value;}
      if(value==target)Phase=0;
    }
    if(Phase!=2 && !(in.Busy & in.Available & ~in.Prepared & 0x1f80u)){
      Phase=2;Base=Add(Base,Delta);Epoch=now;
    }
  }
  return publication;
}
}
