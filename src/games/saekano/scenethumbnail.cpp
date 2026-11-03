#include "scenethumbnail.h"

namespace Impacto::Saekano {
SlotThumbnail PackSceneThumbnailRgba(std::span<const uint8_t> rgba) {
  constexpr size_t count=192*108;
  if(rgba.size()!=count*4)throw FormatError("invalid reduced scene thumbnail size");
  SlotThumbnail result;result.Pixels.resize(count*2);
  for(size_t i=0;i<count;++i) {
    const auto packed=uint16_t((rgba[4*i]>>3) | ((rgba[4*i+1]>>2)<<5) | ((rgba[4*i+2]>>3)<<11));
    result.Pixels[2*i]=uint8_t(packed);result.Pixels[2*i+1]=uint8_t(packed>>8);
  }
  return result;
}
}
