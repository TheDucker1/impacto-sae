#include "game.h"
#include "profile.h"
#include "profile_internal.h"

#include <cstdint>

namespace Impacto {
namespace Profile {

namespace Game {
void ConfigureFrontend() {
  AssertIs(LUA_TTABLE);
  WindowName = EnsureGetMember<std::string>("WindowName");
  OpenGLCompatibility =
      TryGetMember<bool>("OpenGLCompatibility").value_or(false);
  WindowIconPath = TryGetMember<std::string>("WindowIconPath");
  CursorArrowPath = TryGetMember<std::string>("CursorArrowPath");
  CursorPointerPath = TryGetMember<std::string>("CursorPointerPath");
  DesignWidth = EnsureGetMember<float>("DesignWidth");
  DesignHeight = EnsureGetMember<float>("DesignHeight");
  HasInit = true;
}

void Configure() {
  ConfigureFrontend();
  LayerCount = EnsureGetMember<int>("LayerCount");
  GameFeatures = EnsureGetMember<GameFeature>("GameFeatures");
  LayFileBigEndian = TryGetMember<bool>("LayFileBigEndian").value_or(false);
  CharaIsMvl = TryGetMember<bool>("CharaIsMvl").value_or(false);
  LayFileTexXMultiplier =
      TryGetMember<float>("LayFileTexXMultiplier").value_or(1.0f);
  LayFileTexYMultiplier =
      TryGetMember<float>("LayFileTexYMultiplier").value_or(1.0f);

  ScreenCaptureCount = TryGetMember<size_t>("ScreenCaptureCount").value_or(0);
  TryGetMember<bool>("UseMoviePriority", UseMoviePriority);
  TryGetMember<bool>("UseWaveEffects", UseWaveEffects);

  TryGetMember<int>("PlatformId", PlatformId);
}
}  // namespace Game
}  // namespace Profile
}  // namespace Impacto
