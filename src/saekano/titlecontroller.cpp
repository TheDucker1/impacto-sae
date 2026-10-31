#include "titlecontroller.h"
#include "commands.h"
#include <algorithm>
#include <bit>

namespace Impacto::Saekano {
void TitleTween::Configure(int32_t from,int32_t to,uint32_t duration,bool enabled,uint64_t now){Value.StartUnsigned(from,to,duration,now);Enabled=enabled;}
void TitleTween::Restart(uint64_t now){Value.StartUnsigned(Value.From,Value.To,Value.Duration,now);Enabled=true;}
void TitleTween::Enable(uint64_t now){if(!Enabled)Restart(now);}
void TitleTween::Reverse(uint64_t now){Value.StartUnsigned(Value.To,Value.From,Value.Duration,now);}
namespace {
uint32_t Setting(const TitleEnvironment& e,uint16_t at){return e.Scalars.at(at);}
void ResetAnimations(TitleState& s,const TitleEnvironment& e,TitleServices& h,uint64_t now){
  s.LogoMove.Configure(0,100,Setting(e,0x248),false,now);
  s.LogoDetail.Configure(0,100,Setting(e,0x248),false,now);
  s.MenuMove.Configure(0,100,Setting(e,0x2e0),false,now);
  s.MenuDetail.Configure(0,100,Setting(e,0x2e8),false,now);
  for(auto& button:s.Buttons)button.Configure(0,100,Setting(e,0x398),false,now);
  s.Reveal.Configure(100,0,Setting(e,0x1b8),false,now);
  h.ResetEffects(h.RandomRange(1,4));
}
}
void StartTitle(TitleState& s,const TitleEnvironment& e,TitleServices& h,uint64_t now){
  s={};s.Started=true;
  s.Fade.Configure(255,0,Setting(e,0x34),false,now);
  s.Idle.Configure(0,std::bit_cast<int32_t>(Setting(e,0x3c)),Setting(e,0x3c),false,now);
  h.ResetEffects(h.RandomRange(1,4));
}
void UpdateTitle(TitleState& s,const TitleEnvironment& e,TitleServices& h,const TitleInput& in,uint64_t now){
  if(!s.Started || s.SceneRequested)throw UnsupportedCommand("title update has no active scene owner");
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  const auto request=[&](int32_t phase,bool immediate=false){if(immediate){s.Phase=phase;s.PendingPhase.reset();}else s.PendingPhase=phase;};
  const auto scene=[&]{if(!s.TargetScene)throw FormatError("title scene destination is not initialized");h.RequestScene(*s.TargetScene);s.SceneRequested=true;};
  const auto choose=[&](int32_t ordinal){
    if(ordinal<0 || ordinal>=4)throw FormatError("title selection outside authored four-button menu");
    s.Selected=ordinal;
    if(ordinal==0){h.ResetSession();h.ClearMessageHistory();s.TargetScene=2;s.Fade.Reverse(now);s.Fade.Enabled=false;request(20);h.PlayMenuVoice(0);h.UiSound(1);}
    else if(ordinal==1){h.ClearMessageHistory();h.UiSound(3);h.PlayMenuVoice(1);request(11);}
    else {s.Fade.Reverse(now);h.UiSound(3);h.PlayMenuVoice(ordinal);request(ordinal==2?13:17);}
  };
  if(s.Phase==0){h.ResetSession();request(1,true);}
  if(s.Phase==1){h.BeginResources(h.VoiceMode());request(2,true);}
  if(s.Phase==2 && h.ResourcesReady()){h.CommitResources();request(3,true);}
  if(s.Phase==3){h.PlayBgmAndMark(0);ResetAnimations(s,e,h,now);s.Fade.Restart(now);s.Idle.Restart(now);request(5,true);}
  if(s.Phase==5){
    s.Fade.Tick(now);h.UpdatePressStartVisual(now);
    if(s.Fade.Finished()){
      if(in.AnyActivity)s.Idle.Restart(now);
      s.Idle.Tick(now);if(s.Idle.Finished())request(22);
      if(in.Start || in.Accept || in.PressStartReleased){
        h.UiSound(0);s.VoiceCharacter=h.RandomRange(0,80)/20;
        const auto divisor=std::bit_cast<int32_t>(Setting(e,0x28));
        if(!divisor)throw FormatError("title voice variant divisor is zero");
        const auto variant=std::min(h.RandomRange(0,100)/divisor,1);
        h.PlayEntryVoice(variant+s.VoiceCharacter*2);s.Reveal.Restart(now);request(6);
      }
    }
  }
  if(s.Phase==6){s.Reveal.Tick(now);if(s.Reveal.Finished()){s.LogoMove.Restart(now);request(7);}}
  if(s.Phase==7){
    s.LogoMove.Tick(now);
    if(s.LogoMove.Value.Sample-s.LogoMove.Value.Epoch>=Setting(e,0x24c)){
      s.LogoDetail.Enable(now);s.LogoDetail.Tick(now);
      if(s.LogoDetail.Finished()){s.MenuMove.Restart(now);request(8);}
    }
  }
  if(s.Phase==8){
    s.MenuMove.Tick(now);
    if(s.MenuMove.Value.Sample-s.MenuMove.Value.Epoch>=Setting(e,0x2e4)){
      s.MenuDetail.Enable(now);s.MenuDetail.Tick(now);
      if(s.MenuMove.Finished() && s.MenuDetail.Finished()){s.Buttons[0].Restart(now);request(9);}
    }
  }
  if(s.Phase==9)for(size_t i=0;i<s.Buttons.size();++i){s.Buttons[i].Tick(now);if(s.Buttons[i].Finished()){if(i==3)request(10);else s.Buttons[i+1].Enable(now);}}
  if(s.Phase==10){
    s.Selected=h.UpdateMenuNavigation(s.Selected);
    if(in.Accept)choose(s.Selected);
    else if(in.Cancel){h.UiSound(4);ResetAnimations(s,e,h,now);s.Idle.Restart(now);request(5,true);}
    else if(in.ReleasedMenuButton){
      // The pointer branch only clears text history for New Game; the keyboard
      // Continue branch above also clears it. Preserve that native difference.
      if(*in.ReleasedMenuButton>=4)throw FormatError("title pointer ordinal is invalid");
      const auto ordinal=int32_t(*in.ReleasedMenuButton);
      if(ordinal==1){s.Selected=1;h.UiSound(3);h.PlayMenuVoice(1);request(11);}
      else choose(ordinal);
    }
  }
  if(s.Phase==11){h.BeginSubmenu(TitleSubmenu::Load);request(12);}
  if(s.Phase==12){
    const auto status=h.UpdateSubmenu(TitleSubmenu::Load);
    if(status==3){s.TargetScene=2;h.StopBgm();scene();}
    else if(status==2){request(10);h.DestroySubmenu(TitleSubmenu::Load);}
  }
  if(s.Phase==13){s.Fade.Tick(now);if(s.Fade.Finished()){h.BeginSubmenu(TitleSubmenu::Config);request(15);}}
  if(s.Phase==15 && h.UpdateSubmenu(TitleSubmenu::Config)==1){h.DestroySubmenu(TitleSubmenu::Config);s.Fade.Reverse(now);request(19,true);}
  if(s.Phase==16){s.Fade.Tick(now);if(s.Fade.Finished())request(10);}
  if(s.Phase==17){s.Fade.Tick(now);if(s.Fade.Finished()){h.BeginSubmenu(TitleSubmenu::Extras);request(18);}}
  if(s.Phase==18 && h.UpdateSubmenu(TitleSubmenu::Extras)==2){h.DestroySubmenu(TitleSubmenu::Extras);s.Fade.Reverse(now);request(19,true);}
  if(s.Phase==19){s.Fade.Tick(now);if(s.Fade.Finished())request(10);}
  if(s.Phase==20){
    if(h.VoiceMode()==4){if(h.ChannelPlaying(6))request(21);}
    else if(h.ChannelPlaying(6) && h.ChannelPlaying(7))request(21);
  }
  if(s.Phase==21){
    if(h.ChannelPlaying(6) || h.ChannelPlaying(7)){
      if(in.Accept || in.Cancel){h.StopVoice(7);h.StopVoice(6);}
    }else{s.Fade.Enable(now);s.Fade.Tick(now);if(s.Fade.Finished()){h.StopBgm();scene();}}
  }
  if(s.Phase==22){h.StopBgm();h.PrepareMovie();h.RefreshMovie();s.MovieFade.Configure(0,255,Setting(e,0x40),true,now);request(23);}
  if(s.Phase==23){s.MovieFade.Tick(now);if(s.MovieFade.Finished() && h.MovieReady())request(24);}
  if(s.Phase==24){h.StartMovie();request(25);}
  if(s.Phase==25){if(!h.MovieBusy()){h.StopMovie();if(h.MovieFinished())request(26);}if(in.Accept)request(26);}
  if(s.Phase==26){if(s.MovieFade.Value.To==255)s.MovieFade.Reverse(now);request(27);}
  if(s.Phase==27){s.MovieFade.Tick(now);h.RefreshMovie();if(s.MovieFade.Finished()){h.StopMovie();request(28);}}
  if(s.Phase==28 && !h.MovieBusy()){h.StopMovie();if(h.MovieFinished())request(29);}
  if(s.Phase==29){h.RefreshMovie();h.PlayBgmAndMark(0);s.Idle.Restart(now);request(5);}
  if(s.Phase>=10)h.UpdateEffects(now);
  h.TickAmbient(now);
}
}
