#pragma once

#include "live2ddriver.h"

namespace Impacto::Saekano {
struct ModelSetupScene {
  int32_t Script = 0;
  size_t Begin = 0, End = 0;
};
// Diagnostic entry points only: start with a fresh known context at the first
// authored load2 and stop before the following non-model command. This does
// not assert that preceding title/menu/profile commands have run.
std::vector<ModelSetupScene> FindModelSetupScenes(GameResources& resources);
SemanticState RunModelSetupScene(GameResources& resources,
                                 SdkLive2dDriver& driver,
                                 const ModelSetupScene& scene);
}  // namespace Impacto::Saekano
