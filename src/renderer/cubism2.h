#pragma once

#include "../formaterror.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#if defined(_WIN32) && defined(IMPACTO_CUBISM2_DX9)
#ifdef IMPACTO_CUBISM2_BUILD
#define IMPACTO_CUBISM2_API __declspec(dllexport)
#else
#define IMPACTO_CUBISM2_API __declspec(dllimport)
#endif
#else
#define IMPACTO_CUBISM2_API
#endif

namespace Impacto {

struct Cubism2Context;
class Cubism2Model;

// Adapter for the selected legacy Cubism2 SDK. The SDK handles parsing,
// deformation, motion blending and drawing. Calls belong to one renderer
// thread.
class Cubism2Sdk {
 public:
  IMPACTO_CUBISM2_API Cubism2Sdk();
  IMPACTO_CUBISM2_API ~Cubism2Sdk();
  IMPACTO_CUBISM2_API std::string_view Version() const;
  IMPACTO_CUBISM2_API void SetTime(uint64_t milliseconds);
  IMPACTO_CUBISM2_API std::unique_ptr<Cubism2Model> LoadModel(
      std::span<const uint8_t> moc, size_t textureCount);

 private:
  std::shared_ptr<Cubism2Context> context_;
};

class Cubism2Model {
 public:
  IMPACTO_CUBISM2_API ~Cubism2Model();
  Cubism2Model(const Cubism2Model&) = delete;
  Cubism2Model& operator=(const Cubism2Model&) = delete;
  IMPACTO_CUBISM2_API float CanvasWidth() const;
  IMPACTO_CUBISM2_API float CanvasHeight() const;
  IMPACTO_CUBISM2_API void SetParameter(std::string_view name, float value,
                                        float weight = 1.0f);
  IMPACTO_CUBISM2_API void AddParameter(std::string_view name, float value,
                                        float weight = 1.0f);
  IMPACTO_CUBISM2_API float Parameter(std::string_view name) const;
  IMPACTO_CUBISM2_API void Update();
  IMPACTO_CUBISM2_API void SaveParameters();
  IMPACTO_CUBISM2_API void RestoreParameters();
  IMPACTO_CUBISM2_API size_t LoadExpression(std::span<const uint8_t> json);
  IMPACTO_CUBISM2_API std::array<float, 4> ExpressionEyeTargets(
      size_t expression) const;
  IMPACTO_CUBISM2_API int StartExpression(size_t expression);
  IMPACTO_CUBISM2_API void ApplyExpression();
  IMPACTO_CUBISM2_API void StopExpressions();
  IMPACTO_CUBISM2_API bool ExpressionFinished(int handle) const;
  IMPACTO_CUBISM2_API void LoadPose(std::span<const uint8_t> json);
  IMPACTO_CUBISM2_API void ApplyPose();

  // Resource indexes and channel ordering are selected by the game wrapper.
  // The four managers correspond to independent SDK motion queues, allowing the
  // game to apply base and overlay channels in its verified native order.
  IMPACTO_CUBISM2_API size_t LoadMotion(std::span<const uint8_t> mtn,
                                        bool loop);
  IMPACTO_CUBISM2_API void SetMotionLoop(size_t motion, bool loop);
  IMPACTO_CUBISM2_API int StartMotion(size_t motion, size_t channel);
  IMPACTO_CUBISM2_API bool MotionFinished(size_t channel, int handle) const;
  IMPACTO_CUBISM2_API void ApplyMotionChannel(size_t channel);
  IMPACTO_CUBISM2_API void StopMotionChannel(size_t channel);
  IMPACTO_CUBISM2_API void ConfigureBlink(int interval,
                                          const std::array<int32_t, 3>& motion);
  IMPACTO_CUBISM2_API void ApplyBlink();

  // Native resources belong to the active renderer and outlive model drawing.
  IMPACTO_CUBISM2_API void SetDevice(uintptr_t nativeDevice);
  IMPACTO_CUBISM2_API void BindTexture(size_t index, uintptr_t nativeTexture);
  IMPACTO_CUBISM2_API void SetMatrix(const std::array<float, 16>& matrix);
  IMPACTO_CUBISM2_API void SetColor(float red, float green, float blue,
                                    float alpha);
  IMPACTO_CUBISM2_API void Draw();
  static IMPACTO_CUBISM2_API void BeforeDeviceReset();

 private:
  friend class Cubism2Sdk;
  struct Impl;
  explicit Cubism2Model(std::shared_ptr<Cubism2Context> context,
                        std::span<const uint8_t> moc, size_t textureCount);
  std::unique_ptr<Impl> impl_;
};

}  // namespace Impacto
