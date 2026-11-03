#pragma once

#include "gauge.h"
#include "image.h"
#include "imagesave.h"
#include "uisprite.h"

namespace Impacto::Saekano {
struct SemanticState;
struct SceneImageState {
  bool Known = false;
  NativeImageSavePrefix Slots;
  std::array<bool, 4> FillRectsKnown{};
  std::array<std::vector<uint8_t>, 3> Requested;
  std::array<std::vector<uint32_t>, 3> PendingIncrements;
};
ImageSection ImageCategorySection(uint32_t category);
// Validated native FILE_READY restoration. Assets named by slots are loaded;
// references without a slot remain nonresident, as in80071f78/8019ca00.
SceneImageState RestoreSceneImages(const NativeImageSavePrefix& saved,
                                   SceneImageDriver& images);
// Fresh constructor/reset plus owner binding: inactive slots, zero counters,
// extents from the actual BG/CG/free resource pools. Never apply to restores.
SceneImageState FreshSceneImages(SceneImageDriver& images);
// Native801a9024 queued fill and8007708c final white screen fade.
// Render as solid quads with alpha mode1; caller preserves their distinct order.
UiSpriteBlit BuildSceneFill(const NativeSavedFillRect& slot);
std::optional<UiSpriteBlit> BuildScreenFade(int32_t value);
// Native8008b4d4 float-slot draw primitive. Caller owns native scheduling,
// resource readiness, bucket ordering and the actual runtime quake sample.
UiSpriteBlit BuildFloatSceneImage(const NativeSavedFloatImageSlot& slot,
                                  uint32_t category,
                                  std::array<int32_t, 2> quake);
void PollSceneImages(SceneImageState& state, SceneImageDriver& images);
struct FillRectCommand {
  uint32_t Slot = 0, Color = 0;
  int32_t From = 0, To = 0, Duration = 0;
  uint32_t Bucket = 0;
  static FillRectCommand Decode(std::span<const uint8_t> payload);
};
struct FillRectAnimation {
  bool Started = false;
  ScriptLinearTween Opacity;
};
void StartFillRect(SemanticState& state, FillRectAnimation& animation,
                    const FillRectCommand& command, uint64_t now);
bool UpdateFillRect(SemanticState& state, FillRectAnimation& animation,
                     const FillRectCommand& command, bool skipping,
                     uint64_t now);
struct DrawImageCommand {
  uint32_t Route = 0, Category = 0, Slot = 0;
  std::u16string TextureExpression;
  std::array<int32_t, 6> Geometry{};
  uint32_t Bucket = 0;
  int32_t From = 0, To = 0, Duration = 0, Scale = 0;
  uint32_t Owner = 0, Lane = 0;
  static DrawImageCommand Decode(std::span<const uint8_t> payload);
};
struct DrawImageAnimation {
  bool Started = false, Initialized = false;
  int32_t Texture = -1;
  ScriptLinearTween Opacity, Scale;
};
struct ReleaseImageCommand {
  uint32_t Category = 0, Source = 0;
  int32_t IdOrIndex = 0;
  static ReleaseImageCommand Decode(std::span<const uint8_t> payload);
};
void StartReleaseImage(SemanticState& state, const ReleaseImageCommand& command,
                       SceneImageDriver& images);
void StartDrawImage(SemanticState& state, DrawImageAnimation& animation,
                    const DrawImageCommand& command, SceneImageDriver& images,
                    uint64_t now);
bool UpdateDrawImage(SemanticState& state, DrawImageAnimation& animation,
                     const DrawImageCommand& command, SceneImageDriver& images,
                     bool skipping, uint64_t now);
}  // namespace Impacto::Saekano
