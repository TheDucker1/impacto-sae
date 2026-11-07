#pragma once

#include "../yuvframe.h"

namespace Impacto {
namespace OpenGL {

class GLYUVFrame : public YUVFrame {
 public:
  void Init(float width, float height) override;

  void Submit(const void* luma, const void* cb, const void* cr) override;
  void SubmitStrided(const void* luma,int lumaStride,const void* cb,int cbStride,const void* cr,int crStride) override;
  void Release() override;
};

}  // namespace OpenGL
}  // namespace Impacto
