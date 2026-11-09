#include "../scene2d.h"
#include "renderer.h"
#include "shader.h"
#include "utils.h"
#include "../cubism2.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

namespace Impacto::DirectX9 {
namespace {
using Microsoft::WRL::ComPtr;
IDirect3DDevice9* Device() {
  return reinterpret_cast<IDirect3DDevice9*>(
      Impacto::Renderer->NativeDeviceHandle());
}
IDirect3DTexture9* Texture(uint32_t id) {
  return id ? reinterpret_cast<IDirect3DTexture9*>(
                  Impacto::Renderer->NativeTextureHandle(id))
            : nullptr;
}
class State final : public SceneRenderState {
  ComPtr<IDirect3DStateBlock9> Block;
  ComPtr<IDirect3DSurface9> Target, Depth;
  D3DVIEWPORT9 Viewport{};

 public:
  State() {
    auto* d = Device();
    Check(d->CreateStateBlock(D3DSBT_ALL, &Block), "Save scene state");
    Check(d->GetRenderTarget(0, &Target), "Save scene target");
    d->GetDepthStencilSurface(&Depth);
    Check(d->GetViewport(&Viewport), "Save scene viewport");
  }
  ~State() {
    auto* d = Device();
    d->SetRenderTarget(0, Target.Get());
    d->SetDepthStencilSurface(Depth.Get());
    Block->Apply();
    d->SetViewport(&Viewport);
    Impacto::Renderer->InvalidateState();
  }
};
Renderer* Host() { return static_cast<Renderer*>(Impacto::Renderer); }
struct Target final : SceneRenderTarget, DeviceResource {
  ComPtr<IDirect3DSurface9> Surface;
  Target() { Host()->RegisterResource(this); }
  ~Target() {
    Host()->UnregisterResource(this);
    Surface.Reset();
    if (Texture.Handle) Impacto::Renderer->FreeTexture(Texture.Handle);
  }
  void BeforeDeviceReset() override { Surface.Reset(); }
  void AfterDeviceReset() override {
    Check(DirectX9::Texture(Texture.Handle)->GetSurfaceLevel(0, &Surface),
          "Restore scene target surface");
  }
};
D3DBLEND Factor(uint8_t value, bool alpha) {
  switch (value) {
    case 0:
      return D3DBLEND_ZERO;
    case 1:
      return D3DBLEND_ONE;
    case 2:
      return alpha ? D3DBLEND_SRCALPHA : D3DBLEND_SRCCOLOR;
    case 4:
      return D3DBLEND_SRCALPHA;
    case 5:
      return D3DBLEND_INVSRCALPHA;
    default:
      throw FormatError("unsupported scene blend factor");
  }
}
class Pass final : public Scene2D, DeviceResource {
  const std::array<int, 2> CanvasSize;
  ComPtr<IDirect3DVertexDeclaration9> Declaration, ParticleDeclaration;
  Shader UiShader, ParticleShader;
  float FilterMode = 0, FilterValue = 0, AlphaMode = 0;
  void Sampler(DWORD index) {
    auto* d = Device();
    d->SetSamplerState(index, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(index, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(index, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(index, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(index, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  }
  void Stretch(IDirect3DSurface9* from, const RECT* source,
               IDirect3DSurface9* to, const RECT* dest) {
    auto* d = Device();
    const bool active = Host()->IsFrameActive();
    if (active) Check(d->EndScene(), "Suspend scene for target copy");
    const auto result = d->StretchRect(from, source, to, dest, D3DTEXF_LINEAR);
    const auto begin = active ? d->BeginScene() : D3D_OK;
    Check(result, "Copy scene surface");
    Check(begin, "Resume scene after target copy");
  }

 public:
  explicit Pass(std::array<int, 2> canvasSize) : CanvasSize(canvasSize) {
    const D3DVERTEXELEMENT9 desc[] = {
        {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
         0},
        {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,
         0},
        D3DDECL_END()};
    Check(Device()->CreateVertexDeclaration(desc, &Declaration),
          "Create scene quad declaration");
    const D3DVERTEXELEMENT9 particles[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,
         0},
        {0, 12, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,
         0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 0},
        {0, 24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 1},
        {0, 36, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT,
         D3DDECLUSAGE_TEXCOORD, 2},
        D3DDECL_END()};
    Check(Device()->CreateVertexDeclaration(particles, &ParticleDeclaration),
          "Create scene particle declaration");
    UiShader.Compile("Scene2D", Device(), Declaration.Get());
    ParticleShader.Compile("SceneParticle", Device(),
                           ParticleDeclaration.Get());
    Bind();
    Host()->RegisterResource(this);
  }
  ~Pass() { Host()->UnregisterResource(this); }
  void BeforeDeviceReset() override { Cubism2Model::BeforeDeviceReset(); }
  void AfterDeviceReset() override {}
  void Bind() override {
    auto* d = Device();
    UiShader.UseShader(d);
    Sampler(0);
    Sampler(1);
    d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    SetBlend({});
    SetAlphaTest(0);
    SetFilter(0, 0);
  }
  void SetViewport(std::array<int, 4> v) override {
    if (v[0] < 0 || v[1] < 0 || v[2] <= 0 || v[3] <= 0)
      throw FormatError("invalid scene viewport");
    const D3DVIEWPORT9 vp{DWORD(v[0]), DWORD(v[1]), DWORD(v[2]),
                          DWORD(v[3]), 0.f,         1.f};
    Check(Device()->SetViewport(&vp), "Set scene viewport");
  }
  std::array<int, 4> GetViewport() const override {
    D3DVIEWPORT9 v{};
    Check(Device()->GetViewport(&v), "Get scene viewport");
    return {int(v.X), int(v.Y), int(v.Width), int(v.Height)};
  }
  void Clear(std::array<float, 4> c) override {
    auto* d = Device();
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    ComPtr<IDirect3DSurface9> depth;
    d->GetDepthStencilSurface(&depth);
    const auto byte = [](float x) {
      return BYTE(std::clamp(x, 0.f, 1.f) * 255.f);
    };
    Check(
        d->Clear(
            0, nullptr,
            D3DCLEAR_TARGET | (depth ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL : 0),
            D3DCOLOR_ARGB(byte(c[3]), byte(c[0]), byte(c[1]), byte(c[2])), 1.f,
            0),
        "Clear scene target");
  }
  void SetBlend(SceneBlend b) override {
    auto* d = Device();
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, b.Enabled);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SRCBLEND, Factor(b.ColorSource, false));
    d->SetRenderState(D3DRS_DESTBLEND, Factor(b.ColorDestination, false));
    d->SetRenderState(D3DRS_SRCBLENDALPHA, Factor(b.AlphaSource, true));
    d->SetRenderState(D3DRS_DESTBLENDALPHA, Factor(b.AlphaDestination, true));
  }
  void SetFilter(int mode, float value) override {
    if ((mode != 0 && mode != 1) || !std::isfinite(value))
      throw FormatError("invalid scene filter");
    FilterMode = float(mode);
    FilterValue = std::clamp(value, 0.f, 1.f);
  }
  void SetAlphaTest(int mode) override { AlphaMode = float(mode); }
  void SetDepthTest(int mode, bool enabled) override {
    if (mode != 0 && mode != 5)
      throw FormatError("unsupported scene depth comparison");
    auto* d = Device();
    d->SetRenderState(D3DRS_ZENABLE, enabled ? D3DZB_TRUE : D3DZB_FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, enabled);
    d->SetRenderState(D3DRS_ZFUNC, mode == 0 ? D3DCMP_NEVER : D3DCMP_LESSEQUAL);
  }
  void SetStencil(int mode) override {
    auto* d = Device();
    if (!mode) {
      d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
      return;
    }
    if (mode < 1 || mode > 3) throw FormatError("unknown scene stencil mode");
    d->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    d->SetRenderState(D3DRS_STENCILMASK, 255);
    d->SetRenderState(D3DRS_STENCILWRITEMASK, 255);
    d->SetRenderState(D3DRS_STENCILFUNC,
                      mode == 3 ? D3DCMP_EQUAL : D3DCMP_ALWAYS);
    d->SetRenderState(D3DRS_STENCILREF, mode == 1 ? 1 : 0);
    d->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    d->SetRenderState(D3DRS_STENCILZFAIL,
                      mode == 3 ? D3DSTENCILOP_KEEP : D3DSTENCILOP_REPLACE);
    d->SetRenderState(D3DRS_STENCILPASS,
                      mode == 3 ? D3DSTENCILOP_KEEP : D3DSTENCILOP_REPLACE);
  }
  void DrawQuad(const SceneQuad& q, SceneTexture t,
                SceneTexture mask) override {
    auto* d = Device();
    UiShader.UseShader(d);
    Sampler(0);
    Sampler(1);
    const auto vp = GetViewport();
    const float transform[]{-1.f / vp[2], 1.f / vp[3],
                            CanvasSize[0] * 0.5f, CanvasSize[1] * 0.5f};
    d->SetVertexShaderConstantF(0, transform, 1);
    const float tint[]{q.Color[0] / 255.f, q.Color[1] / 255.f,
                       q.Color[2] / 255.f, q.Color[3] / 255.f};
    const float flags[]{float(q.Solid), float(q.Glyph), float(q.AlphaGlyph),
                        float(q.ColoredRule)};
    const float effect[]{float(q.ImageRule), q.RuleValue, FilterMode,
                         FilterValue};
    const float alpha[]{AlphaMode, 0, 0, 0};
    d->SetPixelShaderConstantF(0, tint, 1);
    d->SetPixelShaderConstantF(1, flags, 1);
    d->SetPixelShaderConstantF(2, effect, 1);
    d->SetPixelShaderConstantF(3, alpha, 1);
    d->SetTexture(0, Texture(t.Handle));
    d->SetTexture(1, Texture(mask.Handle));
    std::array<std::array<float, 4>, 6> vertices{};
    constexpr size_t order[]{0, 1, 2, 2, 1, 3};
    for (size_t i = 0; i < 6; ++i) vertices[i] = q.Vertices[order[i]];
    Check(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, vertices.data(),
                             sizeof(vertices[0])),
          "Draw scene quad");
  }
  void DrawParticles(std::span<const SceneParticleVertex, 4> v, SceneTexture t,
                     const SceneParticleUniforms& u) override {
    if (u.AlphaFamily > 6)
      throw FormatError("invalid scene particle alpha family");
    auto state = SaveState();
    auto* d = Device();
    ParticleShader.UseShader(d);
    Sampler(0);
    d->SetVertexShaderConstantF(0, u.Projection.data(), 13);
    d->SetVertexShaderConstantF(13, u.Flip.data(), 1);
    const float target[]{u.TargetSize[0], u.TargetSize[1], u.ViewportEnabled,
                         u.DepthOffset};
    const float depth[]{u.DepthScale, float(u.PresentToPc), 0, 0};
    const auto vp = GetViewport();
    const float offset[]{-1.f / vp[2], 1.f / vp[3], 0, 0};
    d->SetVertexShaderConstantF(14, target, 1);
    d->SetVertexShaderConstantF(15, depth, 1);
    d->SetVertexShaderConstantF(16, offset, 1);
    const float alpha[]{float(u.AlphaFamily), u.AlphaThreshold, 0, 0};
    d->SetPixelShaderConstantF(0, alpha, 1);
    SetBlend(u.Blend);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetTexture(0, Texture(t.Handle));
    Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v.data(),
                             sizeof(SceneParticleVertex)),
          "Draw scene particles");
  }
  std::unique_ptr<SceneRenderState> SaveState() override {
    return std::make_unique<State>();
  }
  void SetScissor(std::optional<std::array<int, 4>> rect) override {
    Device()->SetRenderState(D3DRS_SCISSORTESTENABLE, rect.has_value());
    if (!rect) return;
    const auto v = GetViewport();
    const auto r = *rect;
    RECT area{v[0] + r[0] * v[2] / CanvasSize[0],
              v[1] + r[1] * v[3] / CanvasSize[1],
              v[0] + (r[0] + r[2]) * v[2] / CanvasSize[0],
              v[1] + (r[1] + r[3]) * v[3] / CanvasSize[1]};
    Check(Device()->SetScissorRect(&area), "Set scene scissor");
  }
  void DrawExternal(const std::function<void()>& draw) override {
    auto state = SaveState();
    // Cubism2's D3D9 adapter owns a fixed-function pipeline. As with the GL
    // compatibility pass, begin with the application's programmable shaders
    // off.
    Check(Device()->SetVertexShader(nullptr),
          "Suspend vertex shader for external pass");
    Check(Device()->SetPixelShader(nullptr),
          "Suspend pixel shader for external pass");
    draw();
  }
  std::unique_ptr<SceneRenderTarget> CreateTarget(int width,
                                                  int height) override {
    auto state = SaveState();
    auto t = std::make_unique<Target>();
    t->Texture = {Impacto::Renderer->CreateRenderTexture(width, height),
                  uint32_t(width), uint32_t(height)};
    Check(Texture(t->Texture.Handle)->GetSurfaceLevel(0, &t->Surface),
          "Get scene texture surface");
    SetTarget(*t);
    Clear({0, 0, 0, 0});
    return t;
  }
  void SetTarget(const SceneRenderTarget& target) override {
    const auto& t = static_cast<const Target&>(target);
    Check(Device()->SetDepthStencilSurface(nullptr),
          "Detach scene depth surface");
    Check(Device()->SetRenderTarget(0, t.Surface.Get()), "Set scene target");
    SetViewport({0, 0, int(t.Texture.Width), int(t.Texture.Height)});
  }
  void CopyTarget(SceneRenderTarget& target, std::array<int, 4> v) override {
    auto state = SaveState();
    ComPtr<IDirect3DSurface9> source;
    Check(Device()->GetRenderTarget(0, &source), "Read scene source surface");
    RECT area{v[0], v[1], v[0] + v[2], v[1] + v[3]};
    Stretch(source.Get(), &area, static_cast<Target&>(target).Surface.Get(),
            nullptr);
  }
  void PresentTarget(const SceneRenderTarget& target,
                     std::array<int, 4> v) override {
    auto state = SaveState();
    ComPtr<IDirect3DSurface9> dest;
    Check(Device()->GetRenderTarget(0, &dest),
          "Read scene destination surface");
    RECT area{v[0], v[1], v[0] + v[2], v[1] + v[3]};
    Stretch(static_cast<const Target&>(target).Surface.Get(), nullptr,
            dest.Get(), &area);
  }
  std::vector<uint8_t> ReadTarget(const SceneRenderTarget* target,
                                  std::array<int, 4> area) override {
    auto state = SaveState();
    ComPtr<IDirect3DSurface9> source;
    if (target)
      source = static_cast<const Target&>(*target).Surface;
    else
      Check(Device()->GetRenderTarget(0, &source),
            "Read scene capture surface");
    D3DSURFACE_DESC desc{};
    Check(source->GetDesc(&desc), "Read scene capture dimensions");
    if (area[0] < 0 || area[1] < 0 || area[2] <= 0 || area[3] <= 0 ||
        UINT(area[0] + area[2]) > desc.Width ||
        UINT(area[1] + area[3]) > desc.Height)
      throw FormatError("scene readback area out of range");
    ComPtr<IDirect3DSurface9> staging;
    Check(Device()->CreateOffscreenPlainSurface(desc.Width, desc.Height,
                                                desc.Format, D3DPOOL_SYSTEMMEM,
                                                &staging, nullptr),
          "Create scene readback surface");
    Check(Device()->GetRenderTargetData(source.Get(), staging.Get()),
          "Read scene pixels");
    D3DLOCKED_RECT lock{};
    const RECT rect{area[0], area[1], area[0] + area[2], area[1] + area[3]};
    Check(staging->LockRect(&lock, &rect, D3DLOCK_READONLY),
          "Lock scene readback");
    std::vector<uint8_t> pixels(size_t(area[2]) * area[3] * 4);
    for (int y = 0; y < area[3]; ++y) {
      const auto* row =
          static_cast<const uint8_t*>(lock.pBits) + size_t(y) * lock.Pitch;
      for (int x = 0; x < area[2]; ++x) {
        auto* p = pixels.data() + (size_t(y) * area[2] + x) * 4;
        p[0] = row[x * 4 + 2];
        p[1] = row[x * 4 + 1];
        p[2] = row[x * 4];
        p[3] = desc.Format == D3DFMT_X8R8G8B8 ? 255 : row[x * 4 + 3];
      }
    }
    Check(staging->UnlockRect(), "Unlock scene readback");
    return pixels;
  }
};
}  // namespace
std::unique_ptr<Scene2D> CreateScene2DPass(std::array<int, 2> canvasSize) {
  return std::make_unique<Pass>(canvasSize);
}
}  // namespace Impacto::DirectX9
