#include "optionspreview.h"
#include "resources.h"
#include "commands.h"
namespace Impacto::Saekano {
OptionsPreviewVoices::OptionsPreviewVoices(GameResources& resources,ProfileVoiceOutput& output):output_(output){
  if(resources.SystemVoiceCount(4)<clips_.size())throw FormatError("Options preview bank is incomplete");
  for(size_t i=0;i<clips_.size();++i){
    auto bytes=resources.LoadSystemVoice(4,i).Data;HcaWaveReader checked(bytes);clips_[i]=std::move(bytes);
  }
}
void OptionsPreviewVoices::Prepare(int32_t mode){
  // Mode4 is handled by the page's stop/no-preview branch, never an array read.
  if(mode<0 || size_t(mode)>=clips_.size())throw FormatError("Options preview mode has no sample");
  output_.Prepare(clips_[size_t(mode)]);prepared_=true;
}
void OptionsPreviewVoices::Start(uint64_t now){
  if(!prepared_)throw UnsupportedCommand("Options preview must be prepared before each start");
  output_.Start(now);prepared_=false;
}
}
