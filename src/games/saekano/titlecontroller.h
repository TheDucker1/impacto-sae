#pragma once
#include "title.h"
#include "gauge.h"
#include <optional>

namespace Impacto::Saekano {
enum class TitleSubmenu { Load,Config,Extras };
struct TitleInput {
  bool Accept=false,Cancel=false,Start=false,AnyActivity=false,PressStartReleased=false;
  std::optional<size_t> ReleasedMenuButton;
};
struct TitleTween {
  ScriptLinearTween Value;
  bool Enabled=false;
  void Configure(int32_t from,int32_t to,uint32_t duration,bool enabled,uint64_t now);
  void Restart(uint64_t now);
  void Enable(uint64_t now);
  void Reverse(uint64_t now);
  void Tick(uint64_t now){if(Enabled)Value.Tick(now);}
  bool Finished()const{return Value.Finished();}
};
// Native router boundaries. A frontend must bind these to real owners/resources,
// audio, navigation, movie and submenu implementations before this is playable.
struct TitleServices {
  virtual ~TitleServices()=default;
  virtual uint8_t VoiceMode()const=0;
  virtual void ResetSession()=0;
  virtual void ClearMessageHistory()=0; // Native579e clears the backlog text ring.
  virtual void BeginResources(uint8_t voiceMode)=0;
  virtual bool ResourcesReady()=0;
  virtual void CommitResources()=0;
  virtual void PlayBgmAndMark(int32_t id)=0;
  virtual void StopBgm()=0;
  virtual void UiSound(int32_t id)=0;
  virtual int32_t RandomRange(int32_t lower,int32_t upper)=0;
  virtual void PlayEntryVoice(int32_t index)=0;
  virtual void PlayMenuVoice(int32_t ordinal)=0;
  virtual bool ChannelPlaying(uint32_t channel)=0;
  virtual void StopVoice(uint32_t channel)=0;
  virtual int32_t UpdateMenuNavigation(int32_t selected)=0;
  virtual void BeginSubmenu(TitleSubmenu submenu)=0;
  virtual int32_t UpdateSubmenu(TitleSubmenu submenu)=0;
  virtual void DestroySubmenu(TitleSubmenu submenu)=0;
  virtual void PrepareMovie()=0; // Native OP1.usm, channel0.
  virtual bool MovieReady()=0;
  virtual void StartMovie()=0;
  virtual bool MovieBusy()=0;
  virtual bool MovieFinished()=0;
  virtual void StopMovie()=0;
  virtual uint8_t MovieVolumeOption()const=0; // Persistent BGM option at S60233.
  virtual void SetMovieVolume(int32_t percent)=0;
  virtual void ResetEffects(int32_t variant)=0;
  virtual void UpdatePressStartVisual(uint64_t now)=0;
  virtual void UpdateEffects(uint64_t now)=0;
  virtual void TickAmbient(uint64_t now)=0;
  virtual void RequestScene(int32_t scene)=0;
};
struct TitleState {
  bool Started=false,SceneRequested=false;
  int32_t Phase=0,Selected=0,VoiceCharacter=0;
  std::optional<int32_t> TargetScene;
  std::optional<int32_t> PendingPhase;
  TitleTween Fade,Idle,MovieFade,Reveal,LogoMove,LogoDetail,MenuMove,MenuDetail;
  std::array<TitleTween,4> Buttons;
};
void StartTitle(TitleState& state,const TitleEnvironment& env,TitleServices& host,uint64_t now);
void UpdateTitle(TitleState& state,const TitleEnvironment& env,TitleServices& host,const TitleInput& input,uint64_t now);
}
