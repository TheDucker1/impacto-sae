#include "scenequeue.h"

#include <algorithm>

namespace Impacto::Saekano {
std::vector<SceneDrawPass> BuildSceneDrawQueue(
    const NativeImageSavePrefix& images, const SceneQueueInputs& inputs) {
  std::vector<SceneDrawPass> result;
  auto add = [&](SceneDrawKind kind, uint32_t bucket, uint32_t index = 0) {
    result.push_back({kind, index, bucket});
  };
  if (inputs.ModeVisible) add(SceneDrawKind::ModeUnderlay, 1);
  add(SceneDrawKind::Characters, 8);
  add(SceneDrawKind::Live2d, 9);
  if (inputs.ModeVisible) add(SceneDrawKind::ModeMiddle, 12);
  add(SceneDrawKind::Letterbox, inputs.ForegroundLetterbox ? 25 : 15);
  add(SceneDrawKind::Effect, inputs.EffectPlacement == 1 ? 22 : 15);
  if (!inputs.MessageHidden && inputs.DrawMessage) {
    if (!inputs.SplitMessage) {
      add(SceneDrawKind::MessageWindow, 15);
      add(SceneDrawKind::MessageText, 18);
      add(SceneDrawKind::MessageDecoration, 16);
    } else if (!inputs.SplitMessageHidden) {
      add(SceneDrawKind::SplitFirstWindow, 15);
      add(SceneDrawKind::MessageText, 18);
      add(SceneDrawKind::SplitSecondWindow, 15);
      add(SceneDrawKind::MessageText, 18, 1);
    }
  }
  if (inputs.ModeVisible) add(SceneDrawKind::ModeOverlay, 21);
  for (uint32_t category = 0; category < 2; ++category)
    for (uint32_t i = 0; i < 4; ++i) {
      const auto& slot = images.FloatSlots[category][i];
      if (slot.Texture < -1) throw FormatError("invalid scene image texture");
      if (slot.Texture != -1 && inputs.FloatReady[category][i])
        add(category ? SceneDrawKind::Event : SceneDrawKind::Background,
            slot.Parameters[2], i);
    }
  for (uint32_t i = 0; i < 10; ++i) {
    const auto& slot = images.IntegerSlots[i];
    if (slot.GeometryAndTexture[6] < -1)
      throw FormatError("invalid free image texture");
    if (slot.GeometryAndTexture[6] != -1 && inputs.IntegerReady[i])
      add(SceneDrawKind::FreeImage, slot.Parameter6, i);
  }
  for (uint32_t i = 0; i < 4; ++i)
    if (images.FillRects[i].Opacity != 0)
      add(SceneDrawKind::Fill, images.FillRects[i].Bucket, i);
  for (uint32_t i = 0; i < 3; ++i) {
    add(SceneDrawKind::AnimationGroup, i + 5, i);
    add(SceneDrawKind::AnimationGroup, i + 11, i + 3);
  }
  std::stable_sort(
      result.begin(), result.end(),
      [](const auto& a, const auto& b) { return a.Bucket < b.Bucket; });
  return result;
}
}  // namespace Impacto::Saekano
