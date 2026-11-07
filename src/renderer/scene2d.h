#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Impacto {

// Immediate passes for the older scene pipeline. Textures and the window are
// owned by BaseRenderer; game owners supply geometry, effects and draw order.
struct SceneTexture {
  uint32_t Handle = 0, Width = 0, Height = 0;
  bool Flipped = false;
};
struct SceneBlend {
  bool Enabled = true;
  uint8_t ColorSource = 4, ColorDestination = 5;
  uint8_t AlphaSource = 1, AlphaDestination = 1;
};
struct SceneQuad {
  std::array<std::array<float, 4>, 4>
      Vertices{};  // logical XY and normalized UV
  std::array<uint8_t, 4> Color{255, 255, 255, 255};
  bool Solid = false, Glyph = false, AlphaGlyph = false;
  bool ColoredRule = false, ImageRule = false;
  float RuleValue = 0;
};
struct SceneParticleVertex {
  std::array<float, 3> LocalPosition;
  std::array<uint8_t, 4> Color;
  std::array<float, 2> Uv;
  std::array<float, 3> Position, Angles;
};
struct SceneParticleUniforms {
  std::array<float, 52> Projection;
  std::array<float, 4> Flip;
  std::array<float, 2> TargetSize;
  float ViewportEnabled, DepthOffset, DepthScale, AlphaThreshold;
  bool PresentToPc;
  uint32_t AlphaFamily;
  SceneBlend Blend;
};
class SceneRenderState {
 public:
  virtual ~SceneRenderState() = default;
};
class SceneRenderTarget {
 public:
  virtual ~SceneRenderTarget() = default;
  SceneTexture Texture;
};
class Scene2D {
 public:
  virtual ~Scene2D() = default;
  virtual void Bind() = 0;
  virtual void SetViewport(std::array<int, 4> viewport) = 0;
  virtual std::array<int, 4> GetViewport() const = 0;
  virtual void Clear(std::array<float, 4> color) = 0;
  virtual void SetBlend(SceneBlend blend) = 0;
  virtual void SetFilter(int mode, float value) = 0;
  virtual void SetAlphaTest(int mode) = 0;
  virtual void SetDepthTest(int mode, bool enabled) = 0;
  virtual void SetStencil(int mode) = 0;
  virtual void SetScissor(std::optional<std::array<int, 4>> rectangle) = 0;
  virtual void DrawQuad(const SceneQuad& quad, SceneTexture texture,
                        SceneTexture mask = {}) = 0;
  virtual void DrawParticles(std::span<const SceneParticleVertex, 4> vertices,
                             SceneTexture texture,
                             const SceneParticleUniforms& uniforms) = 0;
  // Captures the complete GPU state, including targets/viewport. Nested SDK,
  // movie and modal passes restore their caller's state on every exit path.
  virtual std::unique_ptr<SceneRenderState> SaveState() = 0;
  virtual void DrawExternal(const std::function<void()>& draw) = 0;
  virtual std::unique_ptr<SceneRenderTarget> CreateTarget(int width,
                                                          int height) = 0;
  virtual void SetTarget(const SceneRenderTarget& target) = 0;
  virtual void CopyTarget(SceneRenderTarget& target,
                          std::array<int, 4> source) = 0;
  virtual void PresentTarget(const SceneRenderTarget& target,
                             std::array<int, 4> destination) = 0;
  // Top-down RGBA, with backend-specific pitch and orientation resolved here.
  virtual std::vector<uint8_t> ReadTarget(const SceneRenderTarget* target,
                                          std::array<int, 4> area) = 0;
};
std::unique_ptr<Scene2D> CreateScene2D();

}  // namespace Impacto
