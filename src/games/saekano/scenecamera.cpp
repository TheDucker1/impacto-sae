#include "scenecamera.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
float AddProduct(float value, float a, float b) {
  volatile float product = a * b;
  return value + product;  // Native VMLA is not fused.
}
float ReverseDifference(float a, float b, float c, float d) {
  volatile float first = a * b, second = c * d;
  return -first + second;  // VNMLS, including its signed-zero behavior.
}
float InverseLength(const std::array<float,3>& value) {
  float lengthSquared = value[1] * value[1];
  lengthSquared = AddProduct(lengthSquared, value[0], value[0]);
  lengthSquared = AddProduct(lengthSquared, value[2], value[2]);
  const float length = std::sqrt(lengthSquared);
  if (!(length > 0) || !std::isfinite(length))
    throw FormatError("degenerate scene camera direction");
  return 1.0f / length;
}
float NegativeDot(const std::array<float,3>& a, const std::array<float,3>& b) {
  float value = a[1] * b[1];
  value = AddProduct(value, a[0], b[0]);
  return -AddProduct(value, a[2], b[2]);
}
}
SceneCameraParameters InitialSceneCamera() {
  SceneCameraParameters result;
  result.Position = {0,0,6}; result.Up = {0,1,0};
  result.Aspect = std::bit_cast<float>(0x3fe1e1e2u);
  result.FieldOfView = std::bit_cast<float>(0x3f490fd8u);
  result.Near = 1; result.Far = 100;
  return result;
}
SceneCameraMatrices UpdateSceneCamera(SceneCameraParameters& parameters) {
  auto next = parameters;
  const auto finite = [](float value) { return std::isfinite(value); };
  for (const auto* vector : {&next.Position, &next.Target, &next.Up})
    if (!std::all_of(vector->begin(), vector->end(), finite))
      throw FormatError("nonfinite scene camera vector");
  if (next.Mode < 0 || next.Mode > 3)
    throw UnsupportedCommand("unsupported scene camera mode");
  if (!finite(next.Aspect) || next.Aspect <= 0 || !finite(next.FieldOfView) ||
      next.FieldOfView <= 0 || next.FieldOfView >= std::bit_cast<float>(0x40490fdbu) ||
      !finite(next.Near) || !finite(next.Far) || next.Near <= 0 || next.Far <= next.Near)
    throw FormatError("invalid scene camera perspective");
  if (next.Mode == 2 || next.Mode == 3) {
    if (!finite(next.OrbitAngles[0]) || !finite(next.OrbitAngles[1]) || !finite(next.OrbitDistance))
      throw FormatError("nonfinite scene camera orbit");
    const auto azimuth = next.OrbitAngles[0], elevation = next.OrbitAngles[1];
    const float radiusCos = next.OrbitDistance * std::cos(elevation);
    const std::array<float,3> orbit{radiusCos * std::cos(azimuth),
                                  next.OrbitDistance * std::sin(elevation),
                                  radiusCos * std::sin(azimuth)};
    if (next.Mode == 2) {
      next.Target = orbit;
      next.Up[1] = next.Up[1] * std::cos(elevation);
    } else {
      next.Position = orbit;
      next.Up[1] = std::cos(elevation);
    }
  }
  auto up = next.Up;
  const auto upInverse = InverseLength(up);
  // Native evaluates normalization in y,z,x order; components independent.
  for (auto& value : up) value = value * upInverse;
  std::array<float,3> back;
  for (size_t i = 0; i < 3; ++i) back[i] = next.Position[i] - next.Target[i];
  const auto backInverse = InverseLength(back);
  for (auto& value : back) value = value * backInverse;
  std::array<float,3> right{
      ReverseDifference(up[2], back[1], up[1], back[2]),
      ReverseDifference(up[0], back[2], up[2], back[0]),
      ReverseDifference(up[1], back[0], up[0], back[1])};
  const auto rightInverse = InverseLength(right);
  for (auto& value : right) value = value * rightInverse;
  up = {ReverseDifference(back[2], right[1], back[1], right[2]),
        ReverseDifference(back[0], right[2], back[2], right[0]),
        ReverseDifference(back[1], right[0], back[0], right[1])};
  SceneCameraMatrices result;
  result.View = {right[0],up[0],back[0],0, right[1],up[1],back[1],0,
                 right[2],up[2],back[2],0, NegativeDot(right,next.Position),
                 NegativeDot(up,next.Position),NegativeDot(back,next.Position),1};
  volatile float halfFov = next.FieldOfView * .5f;
  const float cotangent = std::tan(std::bit_cast<float>(0x3fc90fdbu) - halfFov);
  const float inverseDepth = 1.0f / (next.Near - next.Far);
  result.Projection[0] = cotangent / next.Aspect;
  result.Projection[5] = cotangent;
  result.Projection[10] = (next.Far + next.Near) * inverseDepth;
  result.Projection[11] = -1;
  const float nearFar = next.Far * next.Near;
  const float scaledDepth = nearFar * inverseDepth;
  result.Projection[14] = scaledDepth * 2.0f;
  parameters = next;
  return result;
}
}
