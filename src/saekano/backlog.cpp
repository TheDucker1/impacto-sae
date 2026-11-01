#include "backlog.h"
#include "backlogscreen.h"
#include "commands.h"
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
bool MessageBacklogEnabled(const MessageBacklogGates& gates) {
  return !gates.Battle && !gates.Approach && !gates.Suppressed && !gates.CompletionPolicy;
}
BacklogTextEnvironment BacklogTextEnvironment::Decode(std::span<const uint8_t> bytes) {
  return BacklogEnvironment::Decode(bytes).Text;
}
const BacklogEntry& BacklogHistory::At(size_t index) const {
  if(index>=Count())throw FormatError("backlog index outside retained history");
  return entries_[(oldest_+index)%Capacity];
}
void BacklogHistory::AppendChoice(std::u16string_view label,
    const NativeSlotRecord& working,const BacklogTextEnvironment& environment) {
  const std::u16string text(label);Append(1,255,std::span(&text,1),working,environment);
}
void BacklogHistory::AppendMessage(std::span<const std::u16string> text,bool readColor,
    uint8_t voiceGroup,const NativeSlotRecord& working,const BacklogTextEnvironment& environment,int32_t voiceId) {
  Append(readColor?3:0,voiceGroup,text,working,environment);
  if(!text.empty())entries_[(next_+Capacity-1)%Capacity].Text.back().VoiceId=voiceId;
}
void BacklogHistory::Append(int32_t kind,uint8_t flag,std::span<const std::u16string> text,
    const NativeSlotRecord& working,const BacklogTextEnvironment& environment) {
  if(kind<0 || size_t(kind)>=environment.Colors.size() || !std::isfinite(environment.Scale) || environment.Scale<=0)
    throw FormatError("backlog formatting is unavailable");
  BacklogEntry entry;entry.Kind=kind;entry.Flag=flag;
  for(const auto& source:text)entry.Text.push_back({source,environment.Colors[size_t(kind)],environment.Colors[size_t(kind)],
                                                  environment.LayoutParameter,environment.Scale});
  entry.Record=working;
  // Native5728..574e retires the copied thumbnail, before the ring deep copy.
  entry.Record.ThumbnailPresent=0;entry.Record.Thumbnail.reset();
  if(next_==entries_.size())entries_.push_back(std::move(entry));
  else entries_[next_]=std::move(entry);
  if(count_<Capacity)++count_;else oldest_=(oldest_+1)%Capacity;
  next_=(next_+1)%Capacity;
}
void BacklogHistory::Truncate(size_t count){
  if(count>count_)throw FormatError("backlog truncation exceeds retained history");
  count_=count;next_=(oldest_+count_)%Capacity;
}
void BacklogHistory::Clear(){entries_.clear();oldest_=next_=count_=0;}
}
