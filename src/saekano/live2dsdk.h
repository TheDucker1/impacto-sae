#pragma once

#include "formats.h"

#include <array>
#include <memory>
#include <string_view>

namespace Impacto::Saekano {

struct Cubism2Context;
class Cubism2Model;

// Thin API adapter for the legacy SDK. The SDK remains responsible for parsing,
// deformation, motion blending and drawing. Calls belong to one renderer
// thread.
class Cubism2Sdk {
 public:
  Cubism2Sdk();
  std::string_view Version() const;
  void SetTime(uint64_t milliseconds);
  std::unique_ptr<Cubism2Model> LoadModel(std::span<const uint8_t> moc,
                                          size_t textureCount);

 private:
  std::shared_ptr<Cubism2Context> context_;
};

class Cubism2Model {
 public:
  ~Cubism2Model();
  Cubism2Model(const Cubism2Model&) = delete;
  Cubism2Model& operator=(const Cubism2Model&) = delete;
  float CanvasWidth() const;
  float CanvasHeight() const;
  void SetParameter(std::string_view name, float value, float weight = 1.0f);
  void AddParameter(std::string_view name, float value, float weight = 1.0f);
  float Parameter(std::string_view name) const;
  void Update();
  void SaveParameters();
  void RestoreParameters();
  size_t LoadExpression(std::span<const uint8_t> json);
  std::array<float, 4> ExpressionEyeTargets(size_t expression) const;
  int StartExpression(size_t expression);
  void ApplyExpression();
  void StopExpressions();
  bool ExpressionFinished(int handle) const;
  void LoadPose(std::span<const uint8_t> json);
  void ApplyPose();

  // Resource indexes and channel ordering are selected by the game wrapper.
  // The four managers correspond to independent SDK motion queues, allowing the
  // game to apply base and overlay channels in its verified native order.
  size_t LoadMotion(std::span<const uint8_t> mtn, bool loop);
  void SetMotionLoop(size_t motion, bool loop);
  int StartMotion(size_t motion, size_t channel);
  bool MotionFinished(size_t channel, int handle) const;
  void ApplyMotionChannel(size_t channel);
  void StopMotionChannel(size_t channel);
  void ConfigureBlink(int interval, const std::array<int32_t, 3>& motion);
  void ApplyBlink();

  // OpenGL textures use straight (not premultiplied) RGBA pixels.
  void BindTexture(size_t index, uint32_t openGlTexture);
  void SetMatrix(const std::array<float, 16>& matrix);
  void SetColor(float red, float green, float blue, float alpha);
  void Draw();

 private:
  friend class Cubism2Sdk;
  struct Impl;
  explicit Cubism2Model(std::shared_ptr<Cubism2Context> context,
                        std::span<const uint8_t> moc, size_t textureCount);
  std::unique_ptr<Impl> impl_;
};

}  // namespace Impacto::Saekano
