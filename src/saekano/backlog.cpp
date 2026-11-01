#include "backlog.h"
#include "commands.h"
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
bool MessageBacklogEnabled(const MessageBacklogGates& gates) {
  return !gates.Battle && !gates.Approach && !gates.Suppressed && !gates.CompletionPolicy;
}
BacklogTextEnvironment BacklogTextEnvironment::Decode(std::span<const uint8_t> bytes) {
  PayloadReader r(bytes);BacklogTextEnvironment result;
  const auto skip=[&](size_t count){r.CheckCount(uint32_t(count),1);for(size_t i=0;i<count;++i)r.U8();};
  const auto vector=[&](size_t stride){const auto count=r.U32();r.CheckCount(count,stride);skip(size_t(count)*stride);};
  // Reader801ca250. Helpers1be08c/1bdf10/1be40c consume36/16/8 bytes.
  // These sprite records have a32-bit first field, unlike legacy34-byte sprites.
  skip(3+32+36+16+3*36+8+36+5*8);
  result.LayoutParameter=r.U32(); //801ca644.
  vector(16);skip(2*36+8+4*36+8+2*36+8+4*36+16);
  vector(36);skip(4+2*36+16+3*4);
  result.Scale=std::bit_cast<float>(r.U32()); //801cb21c.
  skip(11*4);vector(4);r.String();
  const auto count=r.U32();r.CheckCount(count,4);
  for(uint32_t i=0;i<count;++i){const auto color=r.U32();result.Colors.push_back(
      {uint8_t(color>>16),uint8_t(color>>8),uint8_t(color),uint8_t(color>>24)});}
  if(r.Remaining() || !std::isfinite(result.Scale) || result.Scale<=0)
    throw FormatError("invalid backlog text environment");
  return result;
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
    uint8_t voiceGroup,const NativeSlotRecord& working,const BacklogTextEnvironment& environment) {
  Append(readColor?3:0,voiceGroup,text,working,environment);
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
  if(Count()<Capacity)entries_.push_back(std::move(entry));
  else {entries_[next_]=std::move(entry);oldest_=(oldest_+1)%Capacity;}
  next_=(next_+1)%Capacity;
}
void BacklogHistory::Clear(){entries_.clear();oldest_=next_=0;}
}
