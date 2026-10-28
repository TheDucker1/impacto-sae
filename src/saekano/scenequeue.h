#pragma once

#include "imagesave.h"

namespace Impacto::Saekano {
// Callback identity, not a claim that every callback's renderer is ported.
enum class SceneDrawKind : uint32_t {
  ModeUnderlay,
  Characters,
  Live2d,
  ModeMiddle,
  Letterbox,
  Effect,
  MessageWindow,
  MessageText,
  MessageDecoration,
  SplitFirstWindow,
  SplitSecondWindow,
  ModeOverlay,
  Background,
  Event,
  FreeImage,
  Fill,
  AnimationGroup,
  ApproachSelection
};
struct SceneDrawPass {
  SceneDrawKind Kind;
  uint32_t Index = 0, Bucket = 0;
  bool operator==(const SceneDrawPass&) const = default;
};
struct SceneQueueInputs {
  bool ModeVisible = false;          // manager+14a5c
  bool ApproachSelectionVisible = false; // incomplete3f6 callback, bucket18
  bool ForegroundLetterbox = false;  // manager+1c60
  int32_t EffectPlacement = 0;       // manager+1bfc; only 1 moves it forward
  bool MessageHidden = false;        // manager+1525
  bool DrawMessage = true;           // native scheduler argument
  bool SplitMessage = false;         // manager+20b4 != 0
  bool SplitMessageHidden = false;   // manager+14a65
  // Snapshot of the per-texture ready queries, not the common loading gate.
  std::array<std::array<bool, 4>, 2> FloatReady{};
  std::array<bool, 10> IntegerReady{};
};
// Native80073158 enqueue order,8009ddce tail insertion and8009de70 traversal.
// Pure pass scheduling: message positioning, runtime quake assignment, resource
// ownership and each callback's rendering remain responsibilities of the
// caller.
std::vector<SceneDrawPass> BuildSceneDrawQueue(
    const NativeImageSavePrefix& images, const SceneQueueInputs& inputs);
}  // namespace Impacto::Saekano
