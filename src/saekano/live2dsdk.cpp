#include "live2dsdk.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Live2D.h>
#include <Live2DModelWinGL.h>
#include <motion/Live2DMotion.h>
#include <motion/EyeBlinkMotion.h>
#include <motion/MotionQueueManager.h>
#include <util/UtSystem.h>
#include <util/Json.h>
#include <L2DExpressionMotion.h>
#include <L2DPose.h>

#include <cassert>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace Impacto::Saekano {
struct Cubism2Context {
  std::thread::id Owner = std::this_thread::get_id();
  Cubism2Context() { live2d::Live2D::init(); }
  ~Cubism2Context() {
    assert(Owner == std::this_thread::get_id());
    live2d::Live2D::dispose();
  }
  void CheckThread() const {
    if (Owner != std::this_thread::get_id())
      throw std::runtime_error("Cubism2 calls must run on the renderer thread");
  }
  void CheckError() const {
    const auto error = live2d::Live2D::getError();
    if (error) throw FormatError("Cubism2 SDK error " + std::to_string(error));
  }
};

namespace {
int InputSize(std::span<const uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > size_t(std::numeric_limits<int>::max()))
    throw FormatError("invalid Cubism2 input size");
  return static_cast<int>(bytes.size());
}
std::string ParameterName(std::string_view name) {
  if (name.empty() || name.find('\0') != std::string_view::npos)
    throw FormatError("invalid Cubism2 parameter name");
  return std::string(name);
}
void Finite(float value) {
  if (!std::isfinite(value)) throw FormatError("non-finite Cubism2 value");
}
}  // namespace

Cubism2Sdk::Cubism2Sdk() {
  static std::mutex mutex;
  static std::weak_ptr<Cubism2Context> active;
  const std::lock_guard guard(mutex);
  context_ = active.lock();
  if (!context_) {
    context_ = std::make_shared<Cubism2Context>();
    active = context_;
  }
  context_->CheckThread();
  context_->CheckError();
}

std::string_view Cubism2Sdk::Version() const {
  context_->CheckThread();
  return live2d::Live2D::getVersionStr();
}
void Cubism2Sdk::SetTime(uint64_t milliseconds) {
  context_->CheckThread();
  if (milliseconds > uint64_t(std::numeric_limits<int64_t>::max()))
    throw FormatError("Cubism2 clock exceeds signed 64-bit range");
  live2d::UtSystem::setUserTimeMSec(static_cast<int64_t>(milliseconds));
}

struct Cubism2Model::Impl {
  // Shared lifetime keeps SDK initialized until every parsed object is
  // released.
  std::shared_ptr<Cubism2Context> Context;
  std::unique_ptr<live2d::Live2DModelWinGL> Model;
  std::vector<std::unique_ptr<live2d::Live2DMotion>> Motions;
  std::vector<std::unique_ptr<live2d::framework::L2DExpressionMotion>>
      Expressions;
  std::unique_ptr<live2d::framework::L2DPose> Pose;
  std::vector<std::array<float, 4>> EyeTargets;
  live2d::EyeBlinkMotion Blink;
  // Queues are destroyed before motions; autoDelete is always false.
  std::array<live2d::MotionQueueManager, 4> Queues;
  live2d::MotionQueueManager ExpressionQueue;
  std::vector<bool> TexturesBound;
  std::array<float, 16> Matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Impl(std::shared_ptr<Cubism2Context> context, std::span<const uint8_t> moc,
       size_t textures)
      : Context(std::move(context)) {
    Context->CheckThread();
    if (textures > size_t(std::numeric_limits<int>::max()))
      throw FormatError("too many Cubism2 textures");
    TexturesBound.resize(textures);
    Model.reset(
        live2d::Live2DModelWinGL::loadModel(moc.data(), InputSize(moc)));
    Context->CheckError();
    if (!Model) throw FormatError("Cubism2 failed to load model");
    // Impacto's decoded GXT pixels retain straight RGB/alpha channels.
    Model->setPremultipliedAlpha(false);
    Model->setMatrix(Matrix.data());
    Model->saveParam();
  }
  ~Impl() { assert(Context->Owner == std::this_thread::get_id()); }
};

Cubism2Model::Cubism2Model(std::shared_ptr<Cubism2Context> context,
                           std::span<const uint8_t> moc, size_t textureCount)
    : impl_(std::make_unique<Impl>(std::move(context), moc, textureCount)) {}
Cubism2Model::~Cubism2Model() = default;
std::unique_ptr<Cubism2Model> Cubism2Sdk::LoadModel(
    std::span<const uint8_t> moc, size_t textureCount) {
  context_->CheckThread();
  return std::unique_ptr<Cubism2Model>(
      new Cubism2Model(context_, moc, textureCount));
}
float Cubism2Model::CanvasWidth() const {
  impl_->Context->CheckThread();
  return impl_->Model->getCanvasWidth();
}
float Cubism2Model::CanvasHeight() const {
  impl_->Context->CheckThread();
  return impl_->Model->getCanvasHeight();
}
void Cubism2Model::SetParameter(std::string_view name, float value,
                                float weight) {
  impl_->Context->CheckThread();
  Finite(value);
  Finite(weight);
  impl_->Model->setParamFloat(ParameterName(name).c_str(), value, weight);
  impl_->Context->CheckError();
}
float Cubism2Model::Parameter(std::string_view name) const {
  impl_->Context->CheckThread();
  const float result = impl_->Model->getParamFloat(ParameterName(name).c_str());
  impl_->Context->CheckError();
  return result;
}
void Cubism2Model::AddParameter(std::string_view name, float value,
                                float weight) {
  impl_->Context->CheckThread();
  Finite(value);
  Finite(weight);
  impl_->Model->addToParamFloat(ParameterName(name).c_str(), value, weight);
  impl_->Context->CheckError();
}
void Cubism2Model::Update() {
  impl_->Context->CheckThread();
  impl_->Model->update();
  impl_->Context->CheckError();
}
void Cubism2Model::SaveParameters() {
  impl_->Context->CheckThread();
  impl_->Model->saveParam();
  impl_->Context->CheckError();
}
void Cubism2Model::RestoreParameters() {
  impl_->Context->CheckThread();
  impl_->Model->loadParam();
  impl_->Context->CheckError();
}
size_t Cubism2Model::LoadExpression(std::span<const uint8_t> json) {
  impl_->Context->CheckThread();
  // Game-side eye transitions query expression targets separately. Reuse the
  // SDK JSON reader; L2DExpressionMotion still owns expression evaluation.
  std::unique_ptr<live2d::Json> metadata(live2d::Json::parseFromBytes(
      reinterpret_cast<const char*>(json.data()), InputSize(json)));
  if (!metadata || metadata->getError())
    throw FormatError("invalid Cubism2 expression metadata");
  std::array<float, 4> targets{1, 1, 1, 1};
  constexpr std::array<const char*, 4> names{
      "PARAM_EYE_L_OPEN", "PARAM_EYE_R_OPEN", "PARAM_EYE_BALL_X",
      "PARAM_EYE_BALL_Y"};
  auto& params = metadata->getRoot()["params"];
  if (!params.isArray())
    throw FormatError("expression params are not an array");
  for (int p = 0; p < params.size(); ++p) {
    auto& parameter = params[p];
    for (size_t eye = 0; eye < names.size(); ++eye) {
      if (!parameter["id"].equals(names[eye])) continue;
      auto& calc = parameter["calc"];
      if (!calc.isNull() && !calc.equals("add"))
        throw FormatError("unverified expression eye target calculation");
      auto& value = parameter["val"];
      if (!value.isDouble()) throw FormatError("invalid expression eye target");
      targets[eye] = static_cast<float>(value.toDouble());
      Finite(targets[eye]);
      break;
    }
  }
  std::unique_ptr<live2d::framework::L2DExpressionMotion> expression(
      live2d::framework::L2DExpressionMotion::loadJson(json.data(),
                                                       InputSize(json)));
  impl_->Context->CheckError();
  if (!expression) throw FormatError("Cubism2 failed to load expression");
  impl_->Expressions.push_back(std::move(expression));
  impl_->EyeTargets.push_back(targets);
  return impl_->Expressions.size() - 1;
}
std::array<float, 4> Cubism2Model::ExpressionEyeTargets(
    size_t expression) const {
  impl_->Context->CheckThread();
  return impl_->EyeTargets.at(expression);
}
int Cubism2Model::StartExpression(size_t expression) {
  impl_->Context->CheckThread();
  const auto handle = impl_->ExpressionQueue.startMotion(
      impl_->Expressions.at(expression).get(), false);
  impl_->Context->CheckError();
  return handle;
}
void Cubism2Model::ApplyExpression() {
  impl_->Context->CheckThread();
  impl_->ExpressionQueue.updateParam(impl_->Model.get());
  impl_->Context->CheckError();
}
void Cubism2Model::StopExpressions() {
  impl_->Context->CheckThread();
  impl_->ExpressionQueue.stopAllMotions();
}
bool Cubism2Model::ExpressionFinished(int handle) const {
  impl_->Context->CheckThread();
  return impl_->ExpressionQueue.isFinished(handle);
}
void Cubism2Model::LoadPose(std::span<const uint8_t> json) {
  impl_->Context->CheckThread();
  impl_->Pose.reset(
      live2d::framework::L2DPose::load(json.data(), InputSize(json)));
  impl_->Context->CheckError();
  if (!impl_->Pose) throw FormatError("Cubism2 failed to load pose");
}
void Cubism2Model::ApplyPose() {
  impl_->Context->CheckThread();
  if (impl_->Pose) impl_->Pose->updateParam(impl_->Model.get());
  impl_->Context->CheckError();
}
size_t Cubism2Model::LoadMotion(std::span<const uint8_t> mtn, bool loop) {
  impl_->Context->CheckThread();
  std::unique_ptr<live2d::Live2DMotion> motion(
      live2d::Live2DMotion::loadMotion(mtn.data(), InputSize(mtn)));
  impl_->Context->CheckError();
  if (!motion) throw FormatError("Cubism2 failed to load motion");
  motion->setLoop(loop);
  impl_->Motions.push_back(std::move(motion));
  return impl_->Motions.size() - 1;
}
int Cubism2Model::StartMotion(size_t motion, size_t channel) {
  impl_->Context->CheckThread();
  const auto result = impl_->Queues.at(channel).startMotion(
      impl_->Motions.at(motion).get(), false);
  impl_->Context->CheckError();
  return result;
}
void Cubism2Model::SetMotionLoop(size_t motion, bool loop) {
  impl_->Context->CheckThread();
  impl_->Motions.at(motion)->setLoop(loop);
}
bool Cubism2Model::MotionFinished(size_t channel, int handle) const {
  impl_->Context->CheckThread();
  return impl_->Queues.at(channel).isFinished(handle);
}
void Cubism2Model::ApplyMotionChannel(size_t channel) {
  impl_->Context->CheckThread();
  impl_->Queues.at(channel).updateParam(impl_->Model.get());
  impl_->Context->CheckError();
}
void Cubism2Model::StopMotionChannel(size_t channel) {
  impl_->Context->CheckThread();
  impl_->Queues.at(channel).stopAllMotions();
}
void Cubism2Model::ConfigureBlink(int interval,
                                  const std::array<int32_t, 3>& motion) {
  impl_->Context->CheckThread();
  if (interval < 0 || motion[0] < 0 || motion[1] < 0 || motion[2] < 0)
    throw FormatError("invalid Cubism2 blink timing");
  impl_->Blink.setInterval(interval);
  impl_->Blink.setEyeMotion(motion[0], motion[1], motion[2]);
}
void Cubism2Model::ApplyBlink() {
  impl_->Context->CheckThread();
  impl_->Blink.setParam(impl_->Model.get());
  impl_->Context->CheckError();
}
void Cubism2Model::BindTexture(size_t index, uint32_t openGlTexture) {
  impl_->Context->CheckThread();
  if (!openGlTexture || index >= impl_->TexturesBound.size())
    throw FormatError("invalid Cubism2 texture binding");
  impl_->Model->setTexture(static_cast<int>(index), openGlTexture);
  impl_->Context->CheckError();
  impl_->TexturesBound[index] = true;
}
void Cubism2Model::SetMatrix(const std::array<float, 16>& matrix) {
  impl_->Context->CheckThread();
  for (float value : matrix) Finite(value);
  impl_->Matrix = matrix;
  impl_->Model->setMatrix(impl_->Matrix.data());
}
void Cubism2Model::SetColor(float red, float green, float blue, float alpha) {
  impl_->Context->CheckThread();
  Finite(red);
  Finite(green);
  Finite(blue);
  Finite(alpha);
  impl_->Model->getDrawParam()->setBaseColor(alpha, red, green, blue);
}
void Cubism2Model::Draw() {
  impl_->Context->CheckThread();
  if (impl_->TexturesBound.empty())
    throw FormatError("Cubism2 draw has no textures");
  for (bool bound : impl_->TexturesBound)
    if (!bound) throw FormatError("Cubism2 draw before textures are bound");
  if (!live2d::DrawParam_OpenGLES2::initGLFuncSuccess)
    live2d::DrawParam_OpenGLES2::initGLFunc();
  if (!live2d::DrawParam_OpenGLES2::initGLFuncSuccess)
    throw std::runtime_error("Cubism2 OpenGL functions are unavailable");
  impl_->Model->draw();
  impl_->Context->CheckError();
}

}  // namespace Impacto::Saekano
