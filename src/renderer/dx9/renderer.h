#pragma once

#include "../renderer.h"
#include "window.h"
#include "shader.h"
#include "yuvframe.h"
#include "nv12frame.h"

#include <d3d9.h>

namespace Impacto {
namespace DirectX9 {

class DeviceResource {
 public:
  virtual ~DeviceResource() = default;
  virtual void BeforeDeviceReset() = 0;
  virtual void AfterDeviceReset() = 0;
};

class Renderer : public BaseRenderer {
 public:
  Renderer();
  ~Renderer() override;
  void Init() override;
  void Shutdown() override;

#ifndef IMPACTO_DISABLE_IMGUI
  void ImGuiBeginFrame() override;
#endif

  void BeginFrame() override;
  void BeginFrame2D() override;
  void EndFrame() override;

  uint32_t MapSpriteSheet(SpriteSheet const& sheet) override { return 0; };
  bool LoadSurf(int surfId, int archiveId, int fileId) override {
    return false;
  };
  void UnloadSurf(int surfId) override {};
  uint32_t SubmitTexture(TexFmt format, uint8_t* buffer, int width,
                         int height) override;
  void ConfigureTextureSampling(uint32_t id, TextureSampling sampling) override;
  void UpdateTexture(uint32_t id, TexFmt format, const uint8_t* pixels,
                     int width, int height, size_t stride) override;
  uintptr_t NativeDeviceHandle() const override {
    return reinterpret_cast<uintptr_t>(Device);
  }
  uintptr_t NativeTextureHandle(uint32_t id) const override;
  uint32_t CreateRenderTexture(int width, int height) override;
  void InvalidateState() override {
    CurrentShader = nullptr;
    CurrentTexture = UINT32_MAX;
  }
  void RegisterResource(DeviceResource* resource) {
    DeviceResources.push_back(resource);
  }
  void UnregisterResource(DeviceResource* resource) {
    std::erase(DeviceResources, resource);
  }
  bool IsFrameActive() const { return Drawing; }

  int GetSpriteSheetImage(SpriteSheet const& sheet,
                          std::span<uint8_t> outBuffer) override;
  void FreeTexture(uint32_t id) override;
  YUVFrame* CreateYUVFrame(float width, float height) override;
  NV12Frame* CreateNV12Frame(float width, float height) override;

  void DrawSprite(const Sprite& sprite, const CornersQuad& dest,
                  glm::mat4 transformation, std::span<const glm::vec4, 4> tints,
                  glm::vec3 colorShift, bool inverted, bool disableBlend,
                  bool textureWrapRepeat) override;

  void DrawMaskedSprite(const Sprite& sprite, const Sprite& mask,
                        const CornersQuad& spriteDest,
                        const CornersQuad& maskDest, int alpha, int fadeRange,
                        glm::mat4 spriteTransformation,
                        glm::mat4 maskTransformation,
                        std::span<const glm::vec4, 4> tints, bool isInverted,
                        bool isSameTexture) override;

  // TODO: implement
  void DrawMaskedBinarySprite(const Sprite& sprite, const Sprite& mask,
                              const CornersQuad& spriteDest,
                              const CornersQuad& maskDest,
                              glm::mat4 spriteTransformation,
                              std::optional<glm::mat4> maskTransformation,
                              std::span<const glm::vec4, 4> tints,
                              bool isInverted) override {}

  void DrawMaskedSpriteOverlay(const Sprite& sprite, const Sprite& mask,
                               const CornersQuad& spriteDest,
                               const CornersQuad& maskDest, int alpha,
                               int fadeRange, glm::mat4 spriteTransformation,
                               glm::mat4 maskTransformation,
                               std::span<const glm::vec4, 4> tints,
                               bool isInverted, bool useMaskAlpha) override;

  void DrawPrimitives(const SpriteSheet& sheet, const SpriteSheet* mask,
                      ShaderProgramType shaderType,
                      std::span<const VertexBufferSprites> vertices,
                      std::span<const uint16_t> indices,
                      glm::mat4 spriteTransformation,
                      glm::mat4 maskTransformation, bool inverted,
                      TopologyMode topologyMode,
                      bool textureWrapRepeat = false) override;

  void DrawCCMessageBox(Sprite const& sprite, Sprite const& mask,
                        RectF const& dest, glm::vec4 tint, int alpha,
                        int fadeRange, float effectCt) override;

  void DrawEdgeDetectedSingleSheetFont(
      const SpriteSheet& sheet, const SpriteSheet* mask,
      std::span<const VertexBufferSprites> vertices,
      std::span<const uint16_t> indices, float differenceFactor,
      float intensityShift, float alphaShift, glm::vec2 renderScale,
      glm::mat4 spriteTransformation, glm::mat4 maskTransformation) override {
  }  // TODO: Implement

  void DrawCHLCCMenuBackground(const Sprite& sprite, const Sprite& mask,
                               const RectF& dest, float alpha) override;

  void DrawBlurredSprite(const Sprite& sprite, const CornersQuad& dest,
                         glm::mat4 transformation,
                         RendererBlurDirection blurDirection,
                         glm::vec4 tint) override {};  // TODO: Implement

  void DrawMosaic(const Sprite& sprite, const CornersQuad dest, float tileSize,
                  glm::mat4 transformation,
                  glm::vec4 tint) override {};  // TODO: Implement

  void DrawVideoTexture(const YUVFrame& frame, const RectF& dest,
                        glm::vec4 tint, bool alphaVideo) override;
  void DrawVideoTexture(const NV12Frame& frame, const RectF& dest,
                        glm::vec4 tint, bool alphaVideo = false) override;
  void DrawSubtitleGlyph(const Sprite& sprite, const CornersQuad& dest,
                         const glm::mat4 transformation,
                         const glm::vec4 tint) override {};  // TODO: Implement

  void CaptureScreencap(Sprite& sprite) override;

  void SetFramebuffer(size_t buffer) override {};  // TODO: Implement
  int GetFramebufferTexture(size_t buffer) override {
    return 0;
  };  // TODO: Implement

  void EnableScissor() override;
  void SetScissorRect(RectF const& rect) override;
  void DisableScissor() override;

  void SetStencilMode(StencilBufferMode mode) override {};  // TODO: implement
  void ClearStencilBuffer() override {};                    // TODO: implement

  void SetBlendMode(RendererBlendMode blendMode) override;

  void Clear(glm::vec4 color) override;

 private:
  void EnsureSpaceAvailable(int vertices, int vertexSize, int indices);
  void EnsureTextureBound(uint32_t texture);
  void EnsureShader(Shader* shader, bool flush = true);
  void Flush() override;

  glm::vec2 DesignToNDC(glm::vec2 designCoord) const override;

  DirectX9Window* DXWindow = nullptr;

  IDirect3D9* Interface = nullptr;
  D3DPRESENT_PARAMETERS PresentParameters{};
  std::vector<DeviceResource*> DeviceResources;
  struct RenderTexture {
    int Width, Height;
    std::vector<uint8_t> Pixels;
  };
  ankerl::unordered_dense::map<uint32_t, RenderTexture> RenderTextures;
  void ResetDevice(int width, int height, bool preserve);
  IDirect3DDevice9* Device = nullptr;
  IDirect3DVertexDeclaration9* VertexDeclaration = nullptr;

  Shader* CurrentShader = nullptr;

  Shader* ShaderSprite = nullptr;
  Shader* ShaderSpriteInverted = nullptr;
  Shader* ShaderMaskedSprite = nullptr;
  Shader* ShaderMaskedSpriteNoAlpha = nullptr;
  Shader* ShaderYUVFrame = nullptr;
  Shader* ShaderNV12Frame = nullptr;
  Shader* ShaderCCMessageBox = nullptr;
  Shader* ShaderCHLCCMenuBackground = nullptr;

  uint32_t CurrentTexture = 0;
  uint32_t NextTextureId = 1;
  ankerl::unordered_dense::map<uint32_t, TextureSampling> TextureSamplers;

  bool Drawing = false;

  static int constexpr VertexBufferSize = 1024 * 1024;
  static int constexpr IndexBufferCount =
      VertexBufferSize / (4 * sizeof(VertexBufferSprites)) * 6;

  uint8_t VertexBuffer[VertexBufferSize];
  IDirect3DVertexBuffer9* VertexBufferDevice = nullptr;
  size_t VertexBufferFill = 0;
  uint16_t IndexBuffer[IndexBufferCount];
  IDirect3DIndexBuffer9* IndexBufferDevice = nullptr;
  IDirect3DIndexBuffer9* IndexBufferMvl = nullptr;
  size_t IndexBufferFill = 0;
};

}  // namespace DirectX9
}  // namespace Impacto
