#pragma once

#include "../impacto.h"

namespace Impacto {

class YUVFrame {
 public:
  virtual ~YUVFrame() = default;
  float Width = 0;
  float Height = 0;
  uint32_t LumaId = 0;
  uint32_t CbId = 0;
  uint32_t CrId = 0;

  virtual void Init(float width, float height) = 0;

  virtual void Submit(const void* luma, const void* cb, const void* cr) = 0;
  virtual void SubmitStrided(const void* luma,int lumaStride,const void* cb,
                             int cbStride,const void* cr,int crStride) {
    const auto copy=[](const void* source,int stride,int width,int height){
      if(!source || stride<width)throw std::invalid_argument("invalid YUV plane layout");
      std::vector<uint8_t> pixels(size_t(width)*height);
      for(int y=0;y<height;++y)std::memcpy(pixels.data()+size_t(y)*width,
          static_cast<const uint8_t*>(source)+size_t(y)*stride,width);
      return pixels;
    };
    const auto y=copy(luma,lumaStride,int(Width),int(Height));
    const auto u=copy(cb,cbStride,int(Width)/2,int(Height)/2);
    const auto v=copy(cr,crStride,int(Width)/2,int(Height)/2);
    Submit(y.data(),u.data(),v.data());
  }
  virtual void Release() = 0;
};

}  // namespace Impacto
