#include "yuvframe.h"
#include "utils.h"
#include <cstring>
namespace Impacto::DirectX9 {
DX9YUVFrame::DX9YUVFrame(IDirect3DDevice9* device) : Device(device) {}
void DX9YUVFrame::Init(float width, float height) {
  Width = width;
  Height = height;
  try {
    Check(Device->CreateTexture(UINT(width), UINT(height), 1, 0, D3DFMT_A8,
                                D3DPOOL_MANAGED, &Luma, nullptr),
          "Create movie luma");
    Check(Device->CreateTexture(UINT(width / 2), UINT(height / 2), 1, 0,
                                D3DFMT_A8, D3DPOOL_MANAGED, &Cb, nullptr),
          "Create movie chroma U");
    Check(Device->CreateTexture(UINT(width / 2), UINT(height / 2), 1, 0,
                                D3DFMT_A8, D3DPOOL_MANAGED, &Cr, nullptr),
          "Create movie chroma V");
  } catch (...) {
    Release();
    throw;
  }
}
void DX9YUVFrame::Submit(const void* y, const void* u, const void* v) {
  SubmitStrided(y, int(Width), u, int(Width) / 2, v, int(Width) / 2);
}
void DX9YUVFrame::SubmitStrided(const void* y, int ys, const void* u, int us,
                                const void* v, int vs) {
  const void* planes[]{y, u, v};
  const int strides[]{ys, us, vs};
  IDirect3DTexture9* textures[]{Luma, Cb, Cr};
  for (int i = 0; i < 3; ++i) {
    const int w = int(Width) / (i ? 2 : 1), h = int(Height) / (i ? 2 : 1);
    if (!planes[i] || strides[i] < w)
      throw FormatError("invalid movie plane layout");
    D3DLOCKED_RECT lock{};
    Check(textures[i]->LockRect(0, &lock, nullptr, 0), "Lock movie plane");
    for (int row = 0; row < h; ++row)
      std::memcpy(
          static_cast<uint8_t*>(lock.pBits) + size_t(row) * lock.Pitch,
          static_cast<const uint8_t*>(planes[i]) + size_t(row) * strides[i], w);
    Check(textures[i]->UnlockRect(0), "Unlock movie plane");
  }
}
void DX9YUVFrame::Release() {
  if (Luma) Luma->Release();
  if (Cb) Cb->Release();
  if (Cr) Cr->Release();
  Luma = Cb = Cr = nullptr;
}
}  // namespace Impacto::DirectX9
