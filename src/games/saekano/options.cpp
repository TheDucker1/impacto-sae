#include "options.h"
#include "commands.h"
#include <algorithm>

namespace Impacto::Saekano {
OptionsValues ReadOptions(const NativeSystemSave& owner){
  OptionsValues result{};std::copy(owner.Bytes6022c.begin(),owner.Bytes6022c.end(),result.begin());
  result[13]=owner.Byte60239;std::copy(owner.Bytes6023a.begin(),owner.Bytes6023a.end(),result.begin()+14);return result;
}
void StoreOptions(NativeSystemSave& owner,const OptionsValues& values){
  std::copy_n(values.begin(),13,owner.Bytes6022c.begin());owner.Byte60239=values[13];
  std::copy_n(values.begin()+14,9,owner.Bytes6023a.begin());
}
bool OptionsChanged(const OptionsValues& original,const OptionsValues& current){
  for(size_t i=0;i<original.size();++i){
    const bool boolean=i==2 || i==4 || i==5 || i==6 || i==12;
    if(boolean?bool(original[i])!=bool(current[i]):original[i]!=current[i])return true;
  }
  return false;
}
void StartOptions(OptionsState& s,const OptionsValues& current,uint32_t duration,uint64_t now){
  s={};s.Started=true;s.Original=current;s.Fade.Configure(255,0,duration,true,now);
}
int32_t UpdateOptions(OptionsState& s,OptionsValues& current,OptionsServices& h,uint64_t now){
  if(!s.Started)throw UnsupportedCommand("options has no active owner");
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  const auto request=[&](int32_t phase,bool immediate=false){if(immediate){s.Phase=phase;s.PendingPhase.reset();}else s.PendingPhase=phase;};
  int32_t result=0;
  if(s.Phase==0)request(1);
  if(s.Phase==1){s.Fade.Restart(now);request(2,true);}
  if(s.Phase==2){s.Fade.Tick(now);if(s.Fade.Finished())request(3);}
  if(s.Phase==3)request(4,true);
  if(s.Phase==4){
    const auto action=h.UpdateControls();
    if(action==1)request(5,true);
    else if(action==2){current=s.Original;h.SynchronizeControls();request(11);}
    else if(action!=0)throw FormatError("options control result outside native domain");
  }
  if(s.Phase==5){
    if(OptionsChanged(s.Original,current)){h.BeginSaveQuestion();request(6,true);}
    else request(11,true);
  }
  if(s.Phase==6){
    const auto dialog=h.UpdateSaveQuestion();
    if(!dialog.Busy){
      if(dialog.Cancelled)request(4,true);
      else if(dialog.Selection==1)request(7,true);
      else if(dialog.Selection==0)request(11,true);
      else request(4,true);
    }
  }
  if(s.Phase==7){h.BeginSystemSave();request(8,true);}
  if(s.Phase==8){const auto save=h.UpdateSystemSave();if(!save.Busy)request(save.NativeResult?11:5,true);}
  if(s.Phase==11){s.Fade.Reverse(now);request(12,true);}
  if(s.Phase==12){s.Fade.Tick(now);if(s.Fade.Finished())request(9,true);}
  if(s.Phase==9){request(10,true);h.BeginVoiceReload(current[13]);}
  if(s.Phase==10 && h.VoiceReloadReady()){
    if(h.ForceSilent())current[7]=current[11]=0;
    h.ApplyAudioOptions();request(13,true);
  }
  if(s.Phase==13){
    if(std::all_of(current.begin()+14,current.end(),[](uint8_t v){return v==0;}))h.RequestTrophy(30);
    bool special=true;for(size_t i=0;i<9;++i)if(bool(current[14+i])!=(i==6)){special=false;break;}
    if(special)h.RequestTrophy(31);
    result=1;
  }
  h.TickDecoration(now);return result;
}
}
