#pragma once

#include "../yuvframe.h"

#include <d3d9.h>

namespace Impacto {
namespace DirectX9 {

class DX9YUVFrame : public YUVFrame {
  friend class Renderer;

 public:
  DX9YUVFrame(IDirect3DDevice9* device);

  void Init(float width, float height) override;

  void Submit(const void* luma, const void* cb, const void* cr) override;
  void SubmitStrided(const void* y, int ys, const void* u, int us,
                     const void* v, int vs) override;
  void Release() override;

 protected:
  IDirect3DDevice9* Device;

  IDirect3DTexture9* Luma = nullptr;
  IDirect3DTexture9* Cb = nullptr;
  IDirect3DTexture9* Cr = nullptr;
};

}  // namespace DirectX9
}  // namespace Impacto
