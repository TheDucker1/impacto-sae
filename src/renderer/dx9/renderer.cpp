#include "renderer.h"

#include <array>
#include <wrl/client.h>

#include "../../profile/game.h"
#include "../../game.h"
#include "../../log.h"
#include "3d/scene.h"
#include "utils.h"

#ifndef IMPACTO_DISABLE_IMGUI
#include <imgui_impl_dx9.h>
#endif

namespace Impacto {
namespace DirectX9 {

Renderer::Renderer() {
  try {
    ImpLog(LogLevel::Info, LogChannel::Render,
           "Initializing Renderer2D DirectX 9 system\n");
    DXWindow = static_cast<DirectX9Window*>(Window);

    Interface = Direct3DCreate9(D3D_SDK_VERSION);
    if (!Interface) throw FormatError("Direct3D9 initialization failed");

    HWND hWnd =
        (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(Window->SDLWindow),
                                     SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

    auto& d3dpp = PresentParameters;
    d3dpp.Windowed = TRUE;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    d3dpp.SwapEffect =
        Window->FullFramebuffer ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(Window->SDLWindow, &width, &height))
      throw FormatError(SDL_GetError());
    d3dpp.BackBufferWidth = width;
    d3dpp.BackBufferHeight = height;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;

    Check(Interface->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp,
                                  &Device),
          "Create Direct3D9 device");
    Check(Device->CreateVertexBuffer(VertexBufferSize, 0, 0, D3DPOOL_MANAGED,
                                     &VertexBufferDevice, NULL),
          "Create sprite vertex buffer");
    Check(Device->CreateIndexBuffer(IndexBufferCount * sizeof(uint16_t), 0,
                                    D3DFMT_INDEX16, D3DPOOL_MANAGED,
                                    &IndexBufferDevice, NULL),
          "Create sprite index buffer");

    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    Device->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
    Device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    Device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_SRCALPHA);
    Device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    std::vector<D3DVERTEXELEMENT9> vertexDeclDesc{
        {0, offsetof(VertexBufferSprites, Position), D3DDECLTYPE_FLOAT2,
         D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, offsetof(VertexBufferSprites, UV), D3DDECLTYPE_FLOAT2,
         D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, offsetof(VertexBufferSprites, Tint), D3DDECLTYPE_FLOAT4,
         D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, offsetof(VertexBufferSprites, MaskUV), D3DDECLTYPE_FLOAT2,
         D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END()};
    Check(Device->CreateVertexDeclaration(vertexDeclDesc.data(),
                                          &VertexDeclaration),
          "Create sprite vertex declaration");

    ShaderSprite = new Shader();
    ShaderSprite->Compile("Sprite", Device, VertexDeclaration);
    ShaderSpriteInverted = new Shader();
    ShaderSpriteInverted->Compile("Sprite_inverted", Device, VertexDeclaration);
    ShaderMaskedSprite = new Shader();
    ShaderMaskedSprite->Compile("MaskedSprite", Device, VertexDeclaration);
    ShaderMaskedSpriteNoAlpha = new Shader();
    ShaderMaskedSpriteNoAlpha->Compile("MaskedSpriteNoAlpha", Device,
                                       VertexDeclaration);
    ShaderYUVFrame = new Shader();
    ShaderYUVFrame->Compile("YUVFrame", Device, VertexDeclaration);
    ShaderNV12Frame = new Shader();
    ShaderNV12Frame->Compile("NV12Frame", Device, VertexDeclaration);
    ShaderCCMessageBox = new Shader();
    ShaderCCMessageBox->Compile("CCMessageBoxSprite", Device,
                                VertexDeclaration);
    ShaderCHLCCMenuBackground = new Shader();
    ShaderCHLCCMenuBackground->Compile("CHLCCMenuBackground", Device,
                                       VertexDeclaration);

    CurrentShader = ShaderSprite;
    CurrentShader->UseShader(Device);

    // Fill index buffer with quads
    size_t index = 0;
    uint16_t vertex = 0;
    while (index + 6 <= IndexBufferCount) {
      // bottom-left -> top-left -> top-right
      IndexBuffer[index] = vertex + 0;
      IndexBuffer[index + 1] = vertex + 1;
      IndexBuffer[index + 2] = vertex + 2;
      // bottom-left -> top-right -> bottom-right
      IndexBuffer[index + 3] = vertex + 0;
      IndexBuffer[index + 4] = vertex + 2;
      IndexBuffer[index + 5] = vertex + 3;
      index += 6;
      vertex += 4;
    }

    // Make 1x1 white pixel for colored rectangles
    Texture rectTexture;
    rectTexture.Load1x1(0xFF, 0xFF, 0xFF, 0xFF);
    SpriteSheet rectSheet(1.0f, 1.0f);
    rectSheet.Texture = rectTexture.Submit(this);
    RectSprite = Sprite(rectSheet, 0.0f, 0.0f, 1.0f, 1.0f);

#ifndef IMPACTO_DISABLE_IMGUI
    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForD3D(DXWindow->SDLWindow);
    ImGui_ImplDX9_Init(Device);
#endif
  } catch (...) {
    Shutdown();
    throw;
  }
}

Renderer::~Renderer() { Shutdown(); }
void Renderer::ResetDevice(int width, int height, bool preserve) {
  using Microsoft::WRL::ComPtr;
  for (auto* resource : DeviceResources) resource->BeforeDeviceReset();
#ifndef IMPACTO_DISABLE_IMGUI
  ImGui_ImplDX9_InvalidateDeviceObjects();
#endif
  for (auto& [id, storage] : RenderTextures) {
    auto* texture = Textures.at(id);
    if (preserve && texture) {
      ComPtr<IDirect3DSurface9> surface, staging;
      Check(texture->GetSurfaceLevel(0, &surface),
            "Get target before device reset");
      Check(Device->CreateOffscreenPlainSurface(
                storage.Width, storage.Height, D3DFMT_A8R8G8B8,
                D3DPOOL_SYSTEMMEM, &staging, nullptr),
            "Create target backup");
      Check(Device->GetRenderTargetData(surface.Get(), staging.Get()),
            "Back up render target");
      D3DLOCKED_RECT lock{};
      Check(staging->LockRect(&lock, nullptr, D3DLOCK_READONLY),
            "Lock target backup");
      storage.Pixels.resize(size_t(storage.Width) * storage.Height * 4);
      for (int y = 0; y < storage.Height; ++y)
        std::memcpy(
            storage.Pixels.data() + size_t(y) * storage.Width * 4,
            static_cast<const uint8_t*>(lock.pBits) + size_t(y) * lock.Pitch,
            size_t(storage.Width) * 4);
      Check(staging->UnlockRect(), "Unlock target backup");
    }
    if (texture) texture->Release();
    Textures.at(id) = nullptr;
  }
  PresentParameters.BackBufferWidth = width;
  PresentParameters.BackBufferHeight = height;
  Check(Device->Reset(&PresentParameters), "Reset Direct3D9 device");
  for (auto& [id, storage] : RenderTextures) {
    IDirect3DTexture9* texture = nullptr;
    Check(Device->CreateTexture(storage.Width, storage.Height, 1,
                                D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT, &texture, nullptr),
          "Restore render texture");
    Textures.at(id) = texture;
    ComPtr<IDirect3DSurface9> surface;
    Check(texture->GetSurfaceLevel(0, &surface), "Restore render surface");
    if (!storage.Pixels.empty()) {
      ComPtr<IDirect3DSurface9> staging;
      Check(Device->CreateOffscreenPlainSurface(
                storage.Width, storage.Height, D3DFMT_A8R8G8B8,
                D3DPOOL_SYSTEMMEM, &staging, nullptr),
            "Create target restore surface");
      D3DLOCKED_RECT lock{};
      Check(staging->LockRect(&lock, nullptr, 0), "Lock target restore");
      for (int y = 0; y < storage.Height; ++y)
        std::memcpy(static_cast<uint8_t*>(lock.pBits) + size_t(y) * lock.Pitch,
                    storage.Pixels.data() + size_t(y) * storage.Width * 4,
                    size_t(storage.Width) * 4);
      Check(staging->UnlockRect(), "Unlock target restore");
      Check(
          Device->UpdateSurface(staging.Get(), nullptr, surface.Get(), nullptr),
          "Restore target pixels");
    } else
      Check(Device->ColorFill(surface.Get(), nullptr, 0),
            "Clear restored target");
  }
  for (auto* resource : DeviceResources) resource->AfterDeviceReset();
  InvalidateState();
}
void Renderer::Init() {
  if (IsInit) return;
  IsInit = true;
  if (+Profile::Game::GameFeatures & +GameFeature::Scene3D) {
    Scene = new Scene3D(DXWindow, Device);
    Scene->Init();
  }
}

void Renderer::Shutdown() {
  if (IsInit && Scene) Scene->Shutdown();
  IsInit = false;
  for (auto& [id, texture] : Textures)
    if (texture) texture->Release();
  Textures.clear();
  RenderTextures.clear();
  TextureSamplers.clear();
  for (auto* shader :
       {ShaderSprite, ShaderSpriteInverted, ShaderMaskedSprite,
        ShaderMaskedSpriteNoAlpha, ShaderYUVFrame, ShaderNV12Frame,
        ShaderCCMessageBox, ShaderCHLCCMenuBackground})
    delete shader;
  ShaderSprite = ShaderSpriteInverted = ShaderMaskedSprite =
      ShaderMaskedSpriteNoAlpha = nullptr;
  ShaderYUVFrame = ShaderNV12Frame = ShaderCCMessageBox =
      ShaderCHLCCMenuBackground = nullptr;
  CurrentShader = nullptr;
  if (VertexDeclaration) VertexDeclaration->Release();
  if (VertexBufferDevice) VertexBufferDevice->Release();
  if (IndexBufferDevice) IndexBufferDevice->Release();
  if (IndexBufferMvl) IndexBufferMvl->Release();
  VertexDeclaration = nullptr;
  VertexBufferDevice = nullptr;
  IndexBufferDevice = IndexBufferMvl = nullptr;
  if (Device) Device->Release();
  if (Interface) Interface->Release();
  Device = nullptr;
  Interface = nullptr;
}

#ifndef IMPACTO_DISABLE_IMGUI
void Renderer::ImGuiBeginFrame() {
  ImGui_ImplDX9_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}
#endif

void Renderer::BeginFrame() {
  if (Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->BeginFrame() called before EndFrame()\n");
    return;
  }

  int width = 0, height = 0;
  if (!SDL_GetWindowSizeInPixels(Window->SDLWindow, &width, &height))
    throw FormatError(SDL_GetError());
  const auto cooperative = Device->TestCooperativeLevel();
  if (cooperative == D3DERR_DEVICELOST) {
    // A fullscreen application may temporarily own the adapter. Pump native
    // window messages while waiting for D3D9 to allow its normal reset.
    while (Device->TestCooperativeLevel() == D3DERR_DEVICELOST) {
      SDL_PumpEvents();
      SDL_Delay(20);
    }
  }
  if (Device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET ||
      UINT(width) != PresentParameters.BackBufferWidth ||
      UINT(height) != PresentParameters.BackBufferHeight)
    ResetDevice(std::max(width, 1), std::max(height, 1), cooperative == D3D_OK);
  Drawing = true;
  VertexBufferFill = 0;
  IndexBufferFill = 0;

  Device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
  Check(Device->BeginScene(), "Begin Direct3D9 frame");
}

void Renderer::BeginFrame2D() {
  Device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  Device->SetRenderState(D3DRS_ZWRITEENABLE, 0);
  Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
}

void Renderer::EndFrame() {
  if (!Drawing) return;
  Flush();
  Drawing = false;

#ifndef IMPACTO_DISABLE_IMGUI
  ImGui::Render();
  ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
#endif

  Check(Device->EndScene(), "End Direct3D9 frame");
  const auto present = Device->Present(NULL, NULL, NULL, NULL);
  if (present != D3DERR_DEVICELOST) Check(present, "Present Direct3D9 frame");
  CurrentShader = 0;
  CurrentTexture = std::numeric_limits<uint32_t>::max();
}

void Renderer::FreeTexture(uint32_t id) {
  if (Drawing) Flush();
  TextureSamplers.erase(id);
  RenderTextures.erase(id);
  if (CurrentTexture == id) {
    Device->SetTexture(0, nullptr);
    CurrentTexture = UINT32_MAX;
  }
  auto textureItr = Textures.find(id);
  if (textureItr != Textures.end()) {
    if (textureItr->second != nullptr) {
      // Immediate scene/SDK passes also bind these resources. Release their
      // device references before dropping the renderer's ownership.
      for (DWORD stage = 0; stage < 16; ++stage) {
        IDirect3DBaseTexture9* bound = nullptr;
        if (SUCCEEDED(Device->GetTexture(stage, &bound)) && bound) {
          if (bound == textureItr->second) Device->SetTexture(stage, nullptr);
          bound->Release();
        }
      }
      textureItr->second->Release();
    }
    Textures.erase(textureItr);
  }
}

YUVFrame* Renderer::CreateYUVFrame(float width, float height) {
  auto frame = std::make_unique<DX9YUVFrame>(Device);
  frame->Init(width, height);
  return frame.release();
}

NV12Frame* Renderer::CreateNV12Frame(float width, float height) {
  auto frame = std::make_unique<DX9NV12Frame>(Device);
  frame->Init(width, height);
  return frame.release();
}

void Renderer::DrawSprite(const Sprite& sprite, const CornersQuad& dest,
                          const glm::mat4 transformation,
                          const std::span<const glm::vec4, 4> tints,
                          const glm::vec3 colorShift, const bool inverted,
                          const bool disableBlend,
                          const bool textureWrapRepeat) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawSprite() called before BeginFrame()\n");
    return;
  }

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  // Are we in sprite mode?
  if (inverted)
    EnsureShader(ShaderSpriteInverted);
  else
    EnsureShader(ShaderSprite);

  // Do we have the texture assigned?
  EnsureTextureBound(sprite.Sheet.Texture);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUV(sprite.Bounds, sprite.Sheet.GetDimensions(), &vertices[0].UV,
            sizeof(VertexBufferSprites));
  QuadSetPosition(CornersQuad(dest).Transform(transformation),
                  &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++) vertices[i].Tint = tints[i];
}

void Renderer::DrawMaskedSprite(
    const Sprite& sprite, const Sprite& mask, const CornersQuad& spriteDest,
    const CornersQuad& maskDest, int alpha, const int fadeRange,
    const glm::mat4 spriteTransformation, const glm::mat4 maskTransformation,
    const std::span<const glm::vec4, 4> tints, const bool isInverted,
    const bool isSameTexture) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawMaskedSpriteOverlay() called before BeginFrame()\n");
    return;
  }

  if (sprite.Sheet.IsScreenCap) Flush();

  alpha = std::clamp(alpha, 0, fadeRange + 256);
  const float alphaRange = 256.0f / fadeRange;
  const float constAlpha = ((255.0f - alpha) * alphaRange) / 255.0f;

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  std::array<float, 4> alphaRes = {alphaRange, constAlpha, 0.0f, 0.0f};
  const BOOL isInvertedB = (BOOL)isInverted;
  const BOOL isSameTextureB = (BOOL)isSameTexture;

  Flush();

  EnsureShader(ShaderMaskedSprite);
  Device->SetTexture(0, Textures[sprite.Sheet.Texture]);
  Device->SetTexture(1, Textures[mask.Sheet.Texture]);

  Device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  Device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  Device->SetPixelShaderConstantF(0, alphaRes.data(), 1);
  Device->SetPixelShaderConstantB(0, &isInvertedB, 1);
  Device->SetPixelShaderConstantB(1, &isSameTextureB, 1);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUV(sprite.Bounds, sprite.Sheet.GetDimensions(), &vertices[0].UV,
            sizeof(VertexBufferSprites));
  QuadSetUV(CornersQuad(maskDest).Transform(maskTransformation),
            sprite.Sheet.GetDimensions(), &vertices[0].MaskUV,
            sizeof(VertexBufferSprites));

  QuadSetPosition(CornersQuad(spriteDest).Transform(spriteTransformation),
                  &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++) vertices[i].Tint = tints[i];
}

void Renderer::DrawMaskedSpriteOverlay(
    const Sprite& sprite, const Sprite& mask, const CornersQuad& spriteDest,
    const CornersQuad& maskDest, int alpha, const int fadeRange,
    const glm::mat4 spriteTransformation, const glm::mat4 maskTransformation,
    const std::span<const glm::vec4, 4> tints, const bool isInverted,
    const bool useMaskAlpha) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawMaskedSpriteOverlay() called before BeginFrame()\n");
    return;
  }

  if (sprite.Sheet.IsScreenCap) Flush();

  if (alpha < 0) alpha = 0;
  if (alpha > fadeRange + 256) alpha = fadeRange + 256;

  float alphaRange = 256.0f / fadeRange;
  float constAlpha = ((255.0f - alpha) * alphaRange) / 255.0f;

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  std::array<float, 4> alphaRes = {alphaRange, constAlpha, 0.0f, 0.0f};
  BOOL isInvertedB = (BOOL)isInverted;
  BOOL isSameTextureB = (BOOL) false;

  if (useMaskAlpha) {
    EnsureShader(ShaderMaskedSprite);
    Device->SetPixelShaderConstantB(1, &isSameTextureB, 1);
  } else {
    EnsureShader(ShaderMaskedSpriteNoAlpha);
  }

  Device->SetTexture(0, Textures[sprite.Sheet.Texture]);
  Device->SetTexture(1, Textures[mask.Sheet.Texture]);

  Device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  Device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  Device->SetPixelShaderConstantF(0, alphaRes.data(), 1);
  Device->SetPixelShaderConstantB(0, &isInvertedB, 1);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUV(sprite.Bounds, sprite.Sheet.GetDimensions(), &vertices[0].UV,
            sizeof(VertexBufferSprites));
  QuadSetUV(CornersQuad(maskDest).Transform(maskTransformation),
            mask.Sheet.GetDimensions(), &vertices[0].MaskUV,
            sizeof(VertexBufferSprites));

  QuadSetPosition(CornersQuad(spriteDest).Transform(spriteTransformation),
                  &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++) vertices[i].Tint = tints[i];
}

void Renderer::DrawPrimitives(
    const SpriteSheet& sheet, const SpriteSheet* const mask,
    const ShaderProgramType shaderType,
    const std::span<const VertexBufferSprites> vertices,
    const std::span<const uint16_t> indices,
    const glm::mat4 spriteTransformation, const glm::mat4 maskTransformation,
    const bool inverted, TopologyMode topologyMode, bool textureWrapRepeat) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawVertices() called before BeginFrame()\n");
    return;
  }

  // The index buffer needs to be flushed
  Flush();

  if (mask != nullptr) {
    std::array<float, 4> alphaRes = {1.0f, 0.0f, 0.0f, 0.0f};
    BOOL isInvertedB = (BOOL)inverted;

    EnsureShader(ShaderMaskedSpriteNoAlpha);

    Device->SetTexture(0, Textures[sheet.Texture]);
    Device->SetTexture(1, Textures[mask->Texture]);

    Device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    Device->SetPixelShaderConstantF(0, alphaRes.data(), 1);
    Device->SetPixelShaderConstantB(0, &isInvertedB, 1);

  } else {
    if (inverted)
      EnsureShader(ShaderSpriteInverted);
    else
      EnsureShader(ShaderSprite);

    EnsureTextureBound(sheet.Texture);
  }

  // Push vertices
  VertexBufferSprites* vertexBuffer =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += vertices.size_bytes();

  const auto vertexInfoToNDC = [this, spriteTransformation,
                                maskTransformation](VertexBufferSprites info) {
    info.Position = DesignToNDC(
        glm::vec2(spriteTransformation * glm::vec4(info.Position, 0.0f, 1.0f)));
    info.MaskUV =
        glm::vec2(maskTransformation * glm::vec4(info.MaskUV, 0.0f, 1.0f));
    return info;
  };
  std::transform(vertices.begin(), vertices.end(), vertexBuffer,
                 vertexInfoToNDC);

  // Push indices
  IndexBufferFill += indices.size();
  std::ranges::copy(indices, IndexBuffer);

  // Flush again and bind back our buffer
  Flush();

  // ReFill index buffer with quads
  size_t index = 0;
  uint16_t vertex = 0;
  while (index + 6 <= IndexBufferCount) {
    // bottom-left -> top-left -> top-right
    IndexBuffer[index] = vertex + 0;
    IndexBuffer[index + 1] = vertex + 1;
    IndexBuffer[index + 2] = vertex + 2;
    // bottom-left -> top-right -> bottom-right
    IndexBuffer[index + 3] = vertex + 0;
    IndexBuffer[index + 4] = vertex + 2;
    IndexBuffer[index + 5] = vertex + 3;
    index += 6;
    vertex += 4;
  }
}

void Renderer::DrawCCMessageBox(Sprite const& sprite, Sprite const& mask,
                                RectF const& dest, glm::vec4 tint, int alpha,
                                int fadeRange, float effectCt) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawCCMessageBox() called before BeginFrame()\n");
    return;
  }

  if (alpha < 0) alpha = 0;
  if (alpha > fadeRange + 256) alpha = fadeRange + 256;

  float alphaRange = 256.0f / fadeRange;
  float constAlpha = ((255.0f - alpha) * alphaRange) / 255.0f;

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  EnsureShader(ShaderCCMessageBox);

  Device->SetTexture(0, Textures[sprite.Sheet.Texture]);
  CurrentTexture = sprite.Sheet.Texture;
  Device->SetTexture(1, Textures[mask.Sheet.Texture]);

  // This is cursed man, idk
  float alphaRes[] = {alphaRange, constAlpha, effectCt, 0.0f};
  Device->SetPixelShaderConstantF(0, alphaRes, 1);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUV(sprite.Bounds, sprite.Sheet.GetDimensions(), &vertices[0].UV,
            sizeof(VertexBufferSprites));
  QuadSetUV(mask.Bounds, mask.Sheet.GetDimensions(), &vertices[0].MaskUV,
            sizeof(VertexBufferSprites));

  QuadSetPosition(dest, &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++) vertices[i].Tint = tint;
}

void Renderer::DrawCHLCCMenuBackground(const Sprite& sprite, const Sprite& mask,
                                       const RectF& dest, float alpha) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawCHLCCMenuBackground() called before BeginFrame()\n");
    return;
  }

  if (alpha < 0.0f)
    alpha = 0;
  else if (alpha > 1.0f)
    alpha = 1.0f;

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  EnsureShader(ShaderCHLCCMenuBackground);

  Device->SetTexture(0, Textures[sprite.Sheet.Texture]);
  CurrentTexture = sprite.Sheet.Texture;
  Device->SetTexture(1, Textures[mask.Sheet.Texture]);

  // This is cursed man, idk
  float alphaRes[] = {alpha, 0.0f, 0.0f, 0.0f};
  Device->GetPixelShaderConstantF(0, alphaRes, 1);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUVFlipped(sprite.Bounds, sprite.Sheet.GetDimensions(), &vertices[0].UV,
                   sizeof(VertexBufferSprites));

  QuadSetUV(mask.Bounds, mask.Sheet.GetDimensions(), &vertices[0].MaskUV,
            sizeof(VertexBufferSprites));

  QuadSetPosition(dest, &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++)
    vertices[i].Tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void Renderer::EnsureSpaceAvailable(int vertices, int vertexSize, int indices) {
  if (VertexBufferFill + vertices * vertexSize > VertexBufferSize ||
      IndexBufferFill + indices > IndexBufferCount) {
    ImpLogSlow(
        LogLevel::Trace, LogChannel::Render,
        "Renderer->EnsureSpaceAvailable flushing because buffers full at "
        "VertexBufferFill=0x{:08x},IndexBufferFill=0x{:08x}\n",
        VertexBufferFill, IndexBufferFill);
    Flush();
  }
}

void Renderer::EnsureTextureBound(uint32_t texture) {
  if (CurrentTexture != texture) {
    ImpLogSlow(LogLevel::Trace, LogChannel::Render,
               "Renderer->EnsureTextureBound flushing because texture {:d} is "
               "not {:d}\n",
               CurrentTexture, texture);
    Flush();
    Device->SetTexture(0, Textures[texture]);
    const auto found = TextureSamplers.find(texture);
    const auto sampling = found == TextureSamplers.end()
                              ? TextureSampling::LinearClamp
                              : found->second;
    const auto filter = sampling == TextureSampling::NearestClamp
                            ? D3DTEXF_POINT
                            : D3DTEXF_LINEAR;
    const auto wrap = sampling == TextureSampling::LinearRepeat
                          ? D3DTADDRESS_WRAP
                          : D3DTADDRESS_CLAMP;
    Device->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
    Device->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSU, wrap);
    Device->SetSamplerState(0, D3DSAMP_ADDRESSV, wrap);
    CurrentTexture = texture;
  }
}

void Renderer::EnsureShader(Shader* shader, bool flush) {
  if (CurrentShader != shader) {
    ImpLogSlow(LogLevel::Debug, LogChannel::Render,
               "Renderer2D changing mode\n");
    if (flush) Flush();
    shader->UseShader(Device);
    CurrentShader = shader;
  }
}

void Renderer::Flush() {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->Flush() called before BeginFrame()\n");
    return;
  }

  if (VertexBufferFill > 0 && IndexBufferFill > 0) {
    VOID* pVoid;
    auto res = VertexBufferDevice->Lock(0, 0, (void**)&pVoid, 0);
    memcpy(pVoid, VertexBuffer, VertexBufferFill);
    res = VertexBufferDevice->Unlock();

    res = IndexBufferDevice->Lock(0, 0, (void**)&pVoid, 0);
    memcpy(pVoid, IndexBuffer, IndexBufferFill * sizeof(uint16_t));
    res = IndexBufferDevice->Unlock();

    auto result = Device->SetStreamSource(0, VertexBufferDevice, 0,
                                          sizeof(VertexBufferSprites));
    if (FAILED(result)) {
      ImpLog(LogLevel::Debug, LogChannel::Render,
             "Failed to set stream source!\n");
      Window->Shutdown();
    }

    result = Device->SetIndices(IndexBufferDevice);
    if (FAILED(result)) {
      ImpLog(LogLevel::Debug, LogChannel::Render,
             "Failed to set stream indices!\n");
      Window->Shutdown();
    }

    result = Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0,
                                          (UINT)IndexBufferFill, 0,
                                          (UINT)(IndexBufferFill / 3));
    if (FAILED(result)) {
      ImpLog(LogLevel::Debug, LogChannel::Render, "Failed to draw!\n");
      Window->Shutdown();
    }
  }
  IndexBufferFill = 0;
  VertexBufferFill = 0;
  CurrentTexture = std::numeric_limits<uint32_t>::max();
}

void Renderer::DrawVideoTexture(const YUVFrame& frame, const RectF& dest,
                                const glm::vec4 tint, const bool alphaVideo) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawVideoTexture() called before BeginFrame()\n");
    return;
  }

  Flush();
  EnsureShader(ShaderYUVFrame);
  for (DWORD plane = 0; plane < 3; ++plane) {
    Device->SetSamplerState(plane, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(plane, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(plane, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    Device->SetSamplerState(plane, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(plane, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  }
  CurrentTexture = std::numeric_limits<uint32_t>::max();

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  auto err = Device->SetTexture(0, static_cast<const DX9YUVFrame&>(frame).Luma);
  err = Device->SetTexture(1, static_cast<const DX9YUVFrame&>(frame).Cb);
  err = Device->SetTexture(2, static_cast<const DX9YUVFrame&>(frame).Cr);

  // This is cursed man, idk
  BOOL alphaVideoB = (BOOL)alphaVideo;
  Device->SetPixelShaderConstantB(0, &alphaVideoB, 1);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUV(RectF(0.0f, 0.0f, frame.Width, frame.Height),
            {frame.Width, frame.Height}, &vertices[0].UV,
            sizeof(VertexBufferSprites));
  QuadSetPosition(dest, &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++) vertices[i].Tint = tint;
}

void Renderer::DrawVideoTexture(const NV12Frame& frame, const RectF& dest,
                                const glm::vec4 tint, const bool alphaVideo) {
  if (!Drawing) {
    ImpLog(LogLevel::Error, LogChannel::Render,
           "Renderer->DrawVideoTexture() called before BeginFrame()\n");
    return;
  }

  Flush();
  EnsureShader(ShaderNV12Frame);
  for (DWORD plane = 0; plane < 2; ++plane) {
    Device->SetSamplerState(plane, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(plane, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(plane, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    Device->SetSamplerState(plane, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetSamplerState(plane, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  }
  CurrentTexture = std::numeric_limits<uint32_t>::max();

  // Do we have space for one more sprite quad?
  EnsureSpaceAvailable(4, sizeof(VertexBufferSprites), 6);

  auto err = Device->SetTexture(0, static_cast<const DX9NV12Frame&>(frame).Luma);
  err = Device->SetTexture(1, static_cast<const DX9NV12Frame&>(frame).CbCr);

  // This is cursed man, idk
  BOOL alphaVideoB = (BOOL)alphaVideo;
  Device->SetPixelShaderConstantB(0, &alphaVideoB, 1);

  // OK, all good, make quad

  VertexBufferSprites* vertices =
      (VertexBufferSprites*)(VertexBuffer + VertexBufferFill);
  VertexBufferFill += 4 * sizeof(VertexBufferSprites);

  IndexBufferFill += 6;

  QuadSetUV(RectF(0.0f, 0.0f, frame.Width, frame.Height),
            {frame.Width, frame.Height}, &vertices[0].UV,
            sizeof(VertexBufferSprites));
  QuadSetPosition(dest, &vertices[0].Position, sizeof(VertexBufferSprites));

  for (int i = 0; i < 4; i++) vertices[i].Tint = tint;
}

void Renderer::CaptureScreencap(Sprite& sprite) {
  Flush();
  if (sprite.Sheet.Texture != 0) {
    FreeTexture(sprite.Sheet.Texture);
  }

  sprite.Sheet.IsScreenCap = true;

  IDirect3DSurface9* sourceSurface = nullptr;
  Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &sourceSurface);

  D3DSURFACE_DESC backBufferDesc;
  sourceSurface->GetDesc(&backBufferDesc);

  sprite.Sheet.DesignWidth = static_cast<float>(backBufferDesc.Width);
  sprite.Sheet.DesignHeight = static_cast<float>(backBufferDesc.Height);
  sprite.Bounds = RectF{0.0f, 0.0f, static_cast<float>(backBufferDesc.Width),
                        static_cast<float>(backBufferDesc.Height)};
  sprite.BaseScale = {Profile::Game::DesignWidth / backBufferDesc.Width,
                      Profile::Game::DesignHeight / backBufferDesc.Height};

  const auto id =
      CreateRenderTexture(backBufferDesc.Width, backBufferDesc.Height);
  auto* texture = reinterpret_cast<IDirect3DTexture9*>(NativeTextureHandle(id));
  sprite.Sheet.Texture = id;

  IDirect3DSurface9* destSurface = nullptr;
  texture->GetSurfaceLevel(0, &destSurface);
  Device->StretchRect(sourceSurface, nullptr, destSurface, nullptr,
                      D3DTEXF_NONE);

  sourceSurface->Release();
  destSurface->Release();
}

void Renderer::EnableScissor() {
  Flush();
  Device->SetRenderState(D3DRS_SCISSORTESTENABLE, true);
}

void Renderer::SetScissorRect(RectF const& rect) {
  Rect viewport = Window->GetViewport();
  float scale = fmin((float)Window->WindowWidth / Profile::Game::DesignWidth,
                     (float)Window->WindowHeight / Profile::Game::DesignHeight);
  float rectX = rect.X * scale;
  float rectY = rect.Y * scale;
  float rectWidth = rect.Width * scale;
  float rectHeight = rect.Height * scale;

  RECT rectW{};
  rectW.left = (LONG)rectX;
  rectW.top = (LONG)rectY;
  rectW.right = (LONG)(rect.X + rectWidth);
  rectW.bottom = (LONG)(rect.Y + rectHeight);
  Device->SetScissorRect(&rectW);
}

void Renderer::DisableScissor() {
  Flush();
  Device->SetRenderState(D3DRS_SCISSORTESTENABLE, false);
}

glm::vec2 Renderer::DesignToNDC(glm::vec2 designCoord) const {
  glm::vec2 result;
  result.x = (designCoord.x / (Profile::Game::DesignWidth * 0.5f)) - 1.0f;
  result.y = 1.0f - (designCoord.y / (Profile::Game::DesignHeight * 0.5f));
  D3DVIEWPORT9 viewport{};
  Check(Device->GetViewport(&viewport), "Read sprite viewport");
  result.x -= 1.0f / viewport.Width;
  result.y += 1.0f / viewport.Height;
  return result;
}

void Renderer::SetBlendMode(RendererBlendMode blendMode) {
  Flush();

  switch (blendMode) {
    case RendererBlendMode::Normal:
      Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
      break;
    case RendererBlendMode::Additive:
      Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
      Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
      break;
    case RendererBlendMode::Premultiplied:
      Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
      Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
      break;
  }
}

void Renderer::Clear(glm::vec4 color) {
  Flush();

  color *= 255;
  D3DCOLOR clearColor =
      D3DCOLOR_ARGB((BYTE)color.a, (BYTE)color.r, (BYTE)color.g, (BYTE)color.b);
  Device->Clear(0, NULL, D3DCLEAR_TARGET, clearColor, 1.0f, 0);
}

}  // namespace DirectX9
}  // namespace Impacto
