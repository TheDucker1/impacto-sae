#include "titleaudio.h"
#include "resources.h"
#include "audio.h"
#include "commands.h"

namespace Impacto::Saekano {
void TitleAudioOwner::RequireReady()const{if(!ready_)throw UnsupportedCommand("title audio resources are not bound");}
void TitleAudioOwner::Load(uint8_t mode){
  if(mode>4)throw UnsupportedCommand("title voice mode outside verified0..4");
  const auto bank=[&](size_t index,size_t required){
    const auto n=resources_.SystemVoiceCount(index);if(n<required || n>4096)throw FormatError("title voice bank has an invalid extent");
    std::vector<Bytes> clips;clips.reserve(n);
    for(size_t i=0;i<n;++i){auto data=resources_.LoadSystemVoice(index,i).Data;HcaWaveReader checked(data);clips.push_back(std::move(data));}
    return clips;
  };
  auto entry=bank(6,8);std::vector<Bytes> menu;
  if(mode!=4)menu=bank(mode,4); // Native mode4 empties the selected menu bank.
  entry_=std::move(entry);menu_=std::move(menu);mode_=mode;ready_=true;
}
void TitleAudioOwner::PlayEntry(int32_t index){RequireReady();if(index<0 || index>=8)throw FormatError("title logo cue outside native eight entries");output_.PlayVoice(entry_.at(size_t(index)));}
void TitleAudioOwner::PlayMenu(int32_t ordinal){
  RequireReady();if(ordinal<0 || ordinal>=4)throw FormatError("title menu cue outside four entries");
  if(!menu_.empty())output_.PlayVoice(menu_.at(size_t(ordinal)));
}
void TitleAudioOwner::PlayUi(int32_t id){RequireReady();if(id<0)throw FormatError("negative title UI sound");output_.PlayUi(id);}
void TitleAudioOwner::Stop(uint32_t channel){RequireReady();if(channel!=6 && channel!=7)throw FormatError("title audio channel outside6/7");output_.Stop(channel);}
bool TitleAudioOwner::Playing(uint32_t channel)const{RequireReady();if(channel!=6 && channel!=7)throw FormatError("title audio channel outside6/7");return output_.Playing(channel);}
void TitleAudioOwner::Retire(){if(!ready_)return;output_.Retire();entry_.clear();menu_.clear();ready_=false;}
}
