#include "renderer.h"
#include "utils.h"

#include <cstring>

namespace Impacto::DirectX9 {
uint32_t Renderer::CreateRenderTexture(int width, int height) {
  if (width <= 0 || height <= 0)
    throw FormatError("invalid render texture size");
  IDirect3DTexture9* texture = nullptr;
  Check(Device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                              D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture,
                              nullptr),
        "Create render texture");
  const auto id = NextTextureId++;
  try {
    Textures.emplace(id, texture);
    RenderTextures.emplace(id, RenderTexture{width, height, {}});
  } catch (...) {
    Textures.erase(id);
    texture->Release();
    throw;
  }
  return id;
}
namespace {
size_t PixelBytes(TexFmt format) {
  switch (format) {
    case TexFmt_RGBA:
      return 4;
    case TexFmt_RGB:
      return 3;
    case TexFmt_U8:
      return 1;
    case TexFmt_UA8:
    case TexFmt_RGB565LowRed:
      return 2;
  }
  throw FormatError("unsupported Direct3D texture format");
}
D3DFORMAT TextureFormat(TexFmt format) {
  switch (format) {
    case TexFmt_RGBA:
    case TexFmt_RGB:
      return D3DFMT_A8R8G8B8;
    case TexFmt_U8:
      return D3DFMT_A8;
    case TexFmt_UA8:
      return D3DFMT_A8L8;
    case TexFmt_RGB565LowRed:
      return D3DFMT_R5G6B5;
  }
  throw FormatError("unsupported Direct3D texture format");
}
}  // namespace

uint32_t Renderer::SubmitTexture(TexFmt format, uint8_t* pixels, int width,
                                 int height) {
  if (!pixels || width <= 0 || height <= 0)
    throw FormatError("invalid texture upload dimensions");
  IDirect3DTexture9* texture = nullptr;
  Check(Device->CreateTexture(width, height, 1, 0, TextureFormat(format),
                              D3DPOOL_MANAGED, &texture, nullptr),
        "Create texture");
  const auto id = NextTextureId++;
  Textures.emplace(id, texture);
  try {
    UpdateTexture(id, format, pixels, width, height,
                  size_t(width) * PixelBytes(format));
  } catch (...) {
    Textures.erase(id);
    texture->Release();
    throw;
  }
  return id;
}

uintptr_t Renderer::NativeTextureHandle(uint32_t id) const {
  const auto it = Textures.find(id);
  if (it == Textures.end() || !it->second)
    throw FormatError("texture is not resident in the renderer");
  return reinterpret_cast<uintptr_t>(it->second);
}

void Renderer::ConfigureTextureSampling(uint32_t id, TextureSampling sampling) {
  (void)NativeTextureHandle(id);
  if (Drawing) Flush();
  TextureSamplers[id] = sampling;
  CurrentTexture = UINT32_MAX;
}

void Renderer::UpdateTexture(uint32_t id, TexFmt format, const uint8_t* pixels,
                             int width, int height, size_t stride) {
  const auto bytes = PixelBytes(format);
  if (!pixels || width <= 0 || height <= 0 || stride < size_t(width) * bytes)
    throw FormatError("invalid texture update layout");
  auto* texture = reinterpret_cast<IDirect3DTexture9*>(NativeTextureHandle(id));
  D3DSURFACE_DESC desc{};
  Check(texture->GetLevelDesc(0, &desc), "Read texture description");
  if (desc.Width != UINT(width) || desc.Height != UINT(height) ||
      desc.Format != TextureFormat(format))
    throw FormatError("texture update does not match allocated storage");
  if (Drawing) Flush();
  D3DLOCKED_RECT locked{};
  Check(texture->LockRect(0, &locked, nullptr, 0), "Lock texture upload");
  for (int y = 0; y < height; ++y) {
    const auto* from = pixels + size_t(y) * stride;
    auto* to = static_cast<uint8_t*>(locked.pBits) + size_t(y) * locked.Pitch;
    if (format == TexFmt_RGB || format == TexFmt_RGBA) {
      for (int x = 0; x < width; ++x) {
        to[x * 4] = from[x * bytes + 2];
        to[x * 4 + 1] = from[x * bytes + 1];
        to[x * 4 + 2] = from[x * bytes];
        to[x * 4 + 3] = format == TexFmt_RGBA ? from[x * bytes + 3] : 255;
      }
    } else if (format == TexFmt_RGB565LowRed) {
      for (int x = 0; x < width; ++x) {
        const uint16_t raw = uint16_t(from[x * 2]) | uint16_t(from[x * 2 + 1])
                                                         << 8;
        const uint16_t rgb =
            uint16_t((raw & 31) << 11 | (raw & 0x7e0) | (raw >> 11));
        to[x * 2] = uint8_t(rgb);
        to[x * 2 + 1] = uint8_t(rgb >> 8);
      }
    } else {
      std::memcpy(to, from, size_t(width) * bytes);
    }
  }
  Check(texture->UnlockRect(0), "Unlock texture upload");
}

int Renderer::GetSpriteSheetImage(const SpriteSheet& sheet,
                                  std::span<uint8_t> out) {
  auto* texture =
      reinterpret_cast<IDirect3DTexture9*>(NativeTextureHandle(sheet.Texture));
  D3DSURFACE_DESC desc{};
  Check(texture->GetLevelDesc(0, &desc), "Read texture description");
  const size_t size = size_t(desc.Width) * desc.Height * 4;
  if (out.size() < size)
    throw FormatError("texture readback buffer is too small");
  D3DLOCKED_RECT lock{};
  Check(texture->LockRect(0, &lock, nullptr, D3DLOCK_READONLY),
        "Lock texture readback");
  for (UINT y = 0; y < desc.Height; ++y) {
    const auto* row =
        static_cast<const uint8_t*>(lock.pBits) + size_t(y) * lock.Pitch;
    for (UINT x = 0; x < desc.Width; ++x) {
      auto* pixel = out.data() + (size_t(y) * desc.Width + x) * 4;
      if (desc.Format == D3DFMT_A8R8G8B8) {
        pixel[0] = row[x * 4 + 2];
        pixel[1] = row[x * 4 + 1];
        pixel[2] = row[x * 4];
        pixel[3] = row[x * 4 + 3];
      } else if (desc.Format == D3DFMT_R5G6B5) {
        const auto value = uint16_t(row[x * 2]) | uint16_t(row[x * 2 + 1]) << 8;
        pixel[0] = uint8_t(((value >> 11) * 255 + 15) / 31);
        pixel[1] = uint8_t((((value >> 5) & 63) * 255 + 31) / 63);
        pixel[2] = uint8_t(((value & 31) * 255 + 15) / 31);
        pixel[3] = 255;
      } else if (desc.Format == D3DFMT_A8) {
        pixel[0] = pixel[1] = pixel[2] = 255;
        pixel[3] = row[x];
      } else {
        pixel[0] = pixel[1] = pixel[2] = row[x * 2];
        pixel[3] = row[x * 2 + 1];
      }
    }
  }
  Check(texture->UnlockRect(0), "Unlock texture readback");
  return int(size);
}
}  // namespace Impacto::DirectX9
