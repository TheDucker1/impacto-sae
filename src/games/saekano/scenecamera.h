#pragma once
#include <array>
#include <cstdint>

namespace Impacto::Saekano {
struct SceneCameraParameters {
  std::array<float,3> Position{}, Target{}, Up{};
  float Aspect = 0, FieldOfView = 0, Near = 0, Far = 0;
  std::array<float,2> OrbitAngles{};
  float OrbitDistance = 0;
  int32_t Mode = 0;
};
struct SceneCameraMatrices {
  std::array<float,16> View{}, Projection{};
};
// Exact authored startup constants, recovered from2696a's a09be call.
SceneCameraParameters InitialSceneCamera();
// Native b0502. Modes2/3 update target/position and up in the supplied state.
SceneCameraMatrices UpdateSceneCamera(SceneCameraParameters& parameters);
}
