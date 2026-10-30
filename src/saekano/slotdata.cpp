#include "slotdata.h"
#include "commands.h"

namespace Impacto::Saekano {
namespace {
constexpr size_t FreshThumbnailBytes=192*108*2;
size_t ThumbnailBytes(uint32_t format) {
  // The game's captured fresh/save path uses internal4 (native05001000).
  // Other formats need a verified row-stride contract before accepting them.
  if(format!=4)throw FormatError("unverified native slot-thumbnail format");
  return FreshThumbnailBytes;
}
}

NativeSlotData NativeSlotData::Decode(std::span<const uint8_t> bytes) {
  if(bytes.size()!=FileBytes)throw FormatError("invalid native slot-data extent");
  PayloadReader r(bytes);NativeSlotData data;data.Records.resize(SlotCount);
  const auto count=[&](size_t stride){const auto n=r.U32();r.CheckCount(n,stride);return n;};
  const auto string=[&](std::vector<uint16_t>& text){const auto n=count(2);text.reserve(n);for(uint32_t i=0;i<n;++i)text.push_back(r.U16());};
  for(auto& s:data.Records) {
    for(auto& v:s.Bytes2b6)v=r.U8();
    if(count(1)!=s.Bytesad8.size())throw FormatError("invalid slot byte-array count");
    for(auto& v:s.Bytesad8)v=r.U8();
    for(auto& v:s.Words1ad8)v=r.U32();
    string(s.Strings[0]);string(s.Strings[1]);s.Word1b54=r.U32();string(s.Strings[2]);
    s.Byte1b90=r.U8();for(auto& v:s.Words1b94)v=r.U32();
    s.Byte1ba4=r.U8();s.Word1ba8=r.U32();
    if(count(2)!=s.Shorts2d4.size())throw FormatError("invalid slot short-array count");
    for(auto& v:s.Shorts2d4)v=r.U16();
    for(auto& v:s.Words2b8)v=r.U32();for(auto& v:s.Words1bac)v=r.U32();
    s.ThumbnailPresent=r.U8();
    if(s.ThumbnailPresent) {
      auto& image=s.Thumbnail.emplace();image.Format=r.U32();
      const auto n=ThumbnailBytes(image.Format);r.CheckCount(uint32_t(n),1);
      image.Pixels.reserve(n);for(size_t i=0;i<n;++i)image.Pixels.push_back(r.U8());
    }
  }
  data.UsedBytes=FileBytes-r.Remaining();
  while(r.Remaining())if(r.U8())throw FormatError("nonzero native slot-data padding");
  return data;
}

Bytes NativeSlotData::Encode()const {
  if(Records.size()!=SlotCount)throw FormatError("invalid native slot-record count");
  Bytes b;b.reserve(FileBytes);
  const auto put=[&](uint64_t value,size_t n){if(n>FileBytes-b.size())throw FormatError("slot-data exceeds native buffer");for(size_t i=0;i<n;++i)b.push_back(uint8_t(value>>(i*8)));};
  const auto string=[&](const std::vector<uint16_t>& text){if(text.size()>FileBytes/2)throw FormatError("slot string exceeds native buffer");put(text.size(),4);for(auto v:text)put(v,2);};
  for(const auto& s:Records) {
    if(bool(s.ThumbnailPresent)!=s.Thumbnail.has_value())throw FormatError("inconsistent slot-thumbnail presence");
    for(auto v:s.Bytes2b6)put(v,1);put(s.Bytesad8.size(),4);for(auto v:s.Bytesad8)put(v,1);
    for(auto v:s.Words1ad8)put(v,4);
    string(s.Strings[0]);string(s.Strings[1]);put(s.Word1b54,4);string(s.Strings[2]);
    put(s.Byte1b90,1);for(auto v:s.Words1b94)put(v,4);put(s.Byte1ba4,1);put(s.Word1ba8,4);
    put(s.Shorts2d4.size(),4);for(auto v:s.Shorts2d4)put(v,2);
    for(auto v:s.Words2b8)put(v,4);for(auto v:s.Words1bac)put(v,4);
    put(s.ThumbnailPresent,1);
    if(s.Thumbnail) {
      const auto& image=*s.Thumbnail;
      if(image.Pixels.size()!=ThumbnailBytes(image.Format))throw FormatError("invalid slot-thumbnail byte extent");
      put(image.Format,4);for(auto v:image.Pixels)put(v,1);
    }
  }
  b.resize(FileBytes,0);return b;
}

NativeSlotData NativeSlotData::Fresh() {
  NativeSlotData data;data.Records.resize(SlotCount);
  for(auto& s:data.Records) {
    s.Words1ad8[2]=0xffffffff;s.Word1b54=0xffffffff;
    s.Words1b94[2]=s.Words1b94[3]=0xffffffff;s.Words2b8[6]=0xffffffff;
    s.ThumbnailPresent=1;s.Thumbnail.emplace();s.Thumbnail->Pixels.resize(FreshThumbnailBytes,0);
  }
  return data;
}
}
