#include "globalui.h"
#include "commands.h"
namespace Impacto::Saekano {
GlobalUiEnvironment GlobalUiEnvironment::Decode(std::span<const uint8_t> bytes){
  PayloadReader r(bytes);GlobalUiEnvironment result;
  for(auto& flag:result.Flags)flag=r.U8();
  for(size_t i=0;i<4;++i)r.String();
  r.U32();r.U32();
  for(size_t i=0;i<9;++i)r.String();
  result.TextHeight=r.U32();const auto duration=r.I32();
  if(duration<0)throw FormatError("negative global UI fade duration");
  result.FadeDuration=uint32_t(duration);result.PrefixBytes=bytes.size()-r.Remaining();
  return result;
}
}
