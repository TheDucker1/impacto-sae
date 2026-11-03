#pragma once
#include "formats.h"

namespace Impacto::Saekano {
class GameResources;
// The same physical voice source7 plays logo and menu cues. Source6 is UI SE.
struct TitleAudioOutput {
  virtual ~TitleAudioOutput()=default;
  virtual void PlayVoice(Bytes encoded)=0;
  virtual void PlayUi(int32_t id)=0;
  virtual void Stop(uint32_t channel)=0;
  // Teardown releases both decoder owners immediately, bypassing stop fades.
  virtual void Retire()=0;
  virtual bool Playing(uint32_t channel)const=0;
};
class TitleAudioOwner {
 public:
  TitleAudioOwner(GameResources& resources,TitleAudioOutput& output):resources_(resources),output_(output){}
  void Load(uint8_t mode);
  bool Ready()const{return ready_;}
  uint8_t Mode()const{return mode_;}
  size_t MenuCount()const{return menu_.size();}
  void PlayEntry(int32_t index);
  void PlayMenu(int32_t ordinal);
  void PlayUi(int32_t id);
  void Stop(uint32_t channel);
  bool Playing(uint32_t channel)const;
  // Explicitly before another scene acquires source6/7. Clears encoded banks
  // only after stopping their independent decoder owners; no dangling binding.
  void Retire();
 private:
  void RequireReady()const;
  GameResources& resources_;TitleAudioOutput& output_;
  bool ready_=false;uint8_t mode_=4;
  std::vector<Bytes> entry_,menu_;
};
}
