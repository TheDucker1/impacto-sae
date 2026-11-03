#include "modelscene.h"
#include "headlessbackend.h"

namespace Impacto::Saekano {
std::vector<ModelSetupScene> FindModelSetupScenes(GameResources& resources) {
  std::vector<ModelSetupScene> scenes;
  for (size_t id = 0; id < resources.ScriptCount(); ++id) {
    ScriptObject script(resources.Load(int32_t(id)));
    const auto& records = script.Records();
    for (size_t i = 0; i < records.size(); ++i) {
      if (records[i].Opcode != 0x438 ||
          DecodeLive2d(script.Payload(i)).Selector != 2)
        continue;
      size_t end = i + 1;
      while (end < records.size() && records[end].Opcode == 0x438) ++end;
      scenes.push_back({int32_t(id), i, end});
      break;
    }
  }
  return scenes;
}
SemanticState RunModelSetupScene(GameResources& resources,
                                 SdkLive2dDriver& driver,
                                 const ModelSetupScene& scene) {
  class Backend final : public HeadlessBackend {
   public:
    SdkLive2dDriver& Driver;
    explicit Backend(SdkLive2dDriver& driver) : Driver(driver) {}
    Live2dDriver* Models() override { return &Driver; }
  } backend(driver);
  if (scene.Begin >= scene.End) throw FormatError("empty model setup scene");
  driver.Reset();
  SemanticState initial;
  initial.ScriptFileId = scene.Script;
  initial.CurrentCommandIndex = int32_t(scene.Begin);
  VirtualMachine vm(resources, backend, initial);
  const auto budget = (scene.End - scene.Begin) * 5;
  for (size_t steps = 0; steps < budget; ++steps) {
    if (vm.State().CurrentCommandIndex == int32_t(scene.End) &&
        (vm.Boundary() == VmBoundary::BeforeStart ||
         vm.Boundary() == VmBoundary::Ended))
      return vm.State();
    if (vm.Opcode() != 0x438)
      throw FormatError("model setup crossed its command block");
    vm.Step(0);
  }
  throw FormatError("model setup did not complete within its boundary budget");
}
}  // namespace Impacto::Saekano
