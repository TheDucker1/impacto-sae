#include "imagelayer.h"

#include "commands.h"

#include <bit>

namespace Impacto::Saekano {
UiSpriteBlit BuildFloatSceneImage(const NativeSavedFloatImageSlot& slot,
                                  uint32_t category,
                                  std::array<int32_t, 2> quake) {
  if (category > 1 || slot.Texture < 0)
    throw FormatError(
        "float scene image requires a BG/CG category and resident texture ID");
  const float scale = float(slot.Parameters[1]) / 100.0f;
  const auto width = int32_t(scale * 960.0f), height = int32_t(scale * 544.0f);
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  UiSpriteBlit result;
  result.Texture = {ProfileTextureSection::SceneImage,
                    int32_t(ImageCategorySection(category)),
                    uint32_t(slot.Texture)};
  result.Destination = {add(add(quake[0], -width / 2), 480),
                        add(add(quake[1], -height / 2), 272), width, height};
  result.Source = {std::bit_cast<int32_t>(slot.Words[0]),
                   std::bit_cast<int32_t>(slot.Words[1]),
                   scale > 0 ? 960 : -960, scale > 0 ? 544 : -544};
  result.Color = {255, 255, 255, slot.Parameters[0]};
  return result;
}
ImageSection ImageCategorySection(uint32_t category) {
  if (category == 0) return ImageSection::Background;
  if (category == 1) return ImageSection::Event;
  if (category == 2) return ImageSection::Free;
  throw FormatError("invalid image category");
}
namespace {
void ValidateSlot(const DrawImageCommand& command) {
  ImageCategorySection(command.Category);
  if (command.Slot >= (command.Category == 2 ? 10U : 4U))
    throw FormatError("image slot outside native category");
}
int32_t Texture(const SceneImageState& state, const DrawImageCommand& command) {
  if (command.Category == 2)
    return state.Slots.IntegerSlots[command.Slot].GeometryAndTexture[6];
  return state.Slots.FloatSlots[command.Category][command.Slot].Texture;
}
int32_t Scale(const SceneImageState& state, const DrawImageCommand& command) {
  if (command.Category == 2)
    return state.Slots.IntegerSlots[command.Slot].Parameter5;
  return state.Slots.FloatSlots[command.Category][command.Slot].Parameters[1];
}
void SetValues(SceneImageState& state, const DrawImageCommand& command,
               int32_t opacity, int32_t scale) {
  if (command.Category == 2) {
    auto& slot = state.Slots.IntegerSlots[command.Slot];
    slot.Parameter4 = uint8_t(opacity);
    slot.Parameter5 = std::bit_cast<int16_t>(uint16_t(scale));
  } else {
    auto& slot = state.Slots.FloatSlots[command.Category][command.Slot];
    slot.Parameters[0] = uint8_t(opacity);
    slot.Parameters[1] = uint8_t(scale);
  }
}
void CheckResource(const SceneImageState& state, uint32_t category,
                   int32_t id) {
  if (id < -1 ||
      (id >= 0 && size_t(id) >= state.Slots.TextureReferences[category].size()))
    throw FormatError("image texture ID outside native reference vector");
}
void Request(SceneImageState& state, SceneImageDriver& images,
             uint32_t category, uint32_t id) {
  const ImageKey key{ImageCategorySection(category), id};
  auto& count = state.Slots.TextureReferences[category][id];
  if (images.AssetReady(key)) {
    ++count;  // Native counters wrap unsigned; no implicit release on
              // overwrite.
  } else {
    images.RequestAsset(key);
    state.Requested[category][id] = 1;
    ++state.PendingIncrements[category][id];
  }
}
}  // namespace
void PollSceneImages(SceneImageState& state, SceneImageDriver& images) {
  if (!state.Known)
    throw UnsupportedCommand(
        "restored image slots/reference counters are missing");
  for (uint32_t category = 0; category < 3; ++category)
    for (uint32_t id = 0; id < state.PendingIncrements[category].size(); ++id) {
      auto& pending = state.PendingIncrements[category][id];
      if (pending && images.AssetReady({ImageCategorySection(category), id})) {
        state.Slots.TextureReferences[category][id] += pending;
        pending = 0;
      }
    }
}
SceneImageState RestoreSceneImages(const NativeImageSavePrefix& saved,
                                   SceneImageDriver& images) {
  // Validate every group/ID before submitting any host load request.
  SceneImageState result;
  result.Slots = saved;
  result.FillRectsKnown.fill(true);
  for (uint32_t category = 0; category < 3; ++category) {
    const auto count = saved.TextureReferences[category].size();
    if (count != images.AssetCount(ImageCategorySection(category)))
      throw FormatError(
          "saved image reference vector differs from original archive");
    result.Requested[category].resize(count);
    result.PendingIncrements[category].resize(count);
  }
  for (uint32_t category = 0; category < 2; ++category)
    for (const auto& slot : saved.FloatSlots[category])
      CheckResource(result, category, slot.Texture);
  for (const auto& slot : saved.IntegerSlots)
    CheckResource(result, 2, slot.GeometryAndTexture[6]);
  // Native decoder clears old wrappers before restoring counters.
  for (uint32_t category = 0; category < 3; ++category)
    for (uint32_t id = 0; id < saved.TextureReferences[category].size(); ++id)
      images.ReleaseAsset({ImageCategorySection(category), id});
  result.Known = true;
  for (uint32_t category = 0; category < 2; ++category)
    for (const auto& slot : saved.FloatSlots[category])
      if (slot.Texture >= 0)
        Request(result, images, category, uint32_t(slot.Texture));
  for (const auto& slot : saved.IntegerSlots)
    if (slot.GeometryAndTexture[6] >= 0)
      Request(result, images, 2, uint32_t(slot.GeometryAndTexture[6]));
  PollSceneImages(result, images);
  return result;
}
SceneImageState FreshSceneImages(SceneImageDriver& images) {
  NativeImageSavePrefix fresh;
  for (uint32_t category = 0; category < 3; ++category) {
    const auto count = images.AssetCount(ImageCategorySection(category));
    if (count > 1000000)
      throw FormatError("fresh image resource pool exceeds budget");
    fresh.TextureReferences[category].resize(count);
  }
  return RestoreSceneImages(fresh, images);
}
FillRectCommand FillRectCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  FillRectCommand result;
  result.Slot = reader.U32();
  result.Color = reader.U32();
  result.From = reader.I32();
  result.To = reader.I32();
  result.Duration = reader.I32();
  result.Bucket = reader.U32();
  if (result.Slot >= 4) throw FormatError("fill rectangle slot outside native four-slot array");
  return result;
}
void StartFillRect(SemanticState& state, FillRectAnimation& animation,
                    const FillRectCommand& command, uint64_t now) {
  if (command.Slot >= 4)
    throw FormatError("fill rectangle slot outside native four-slot array");
  if (command.Duration < 0)
    throw UnsupportedCommand("negative fill rectangle duration is unsupported");
  if (!state.Effects.Known)
    throw UnsupportedCommand("restored common effect context is missing");
  animation.Opacity.Start(command.From, command.To, command.Duration, now);
  animation.Started = true;
  // Saved rows contain 00RRGGBB; LOAD discards authored high byte and the
  // command constructor supplies alpha0. The renderer replaces alpha from
  // the separate opacity word's low byte at draw time.
  state.SceneImages.Slots.FillRects[command.Slot] = {
      command.Color & 0xffffffu, command.From, command.Bucket};
  state.SceneImages.FillRectsKnown[command.Slot] = true;
  state.Effects.CurrentValue = state.Effects.RestoreValue;
  for (uint32_t i = 0; i < 2; ++i)
    state.PendingEvents.push_back({GameEventKind::StopChannel,
        std::bit_cast<int32_t>(uint32_t(state.Effects.FirstAudioChannel) + i)});
}
bool UpdateFillRect(SemanticState& state, FillRectAnimation& animation,
                     const FillRectCommand& command, bool skipping,
                     uint64_t now) {
  if (command.Slot >= 4)
    throw FormatError("fill rectangle slot outside native four-slot array");
  auto& slot = state.SceneImages.Slots.FillRects[command.Slot];
  if (skipping) {
    slot.Opacity = command.To;
    return true;  // Native skip does not tick/overwrite its command tween.
  }
  if (!animation.Started)
    throw UnsupportedCommand("fill rectangle Start was suppressed");
  animation.Opacity.Tick(now);
  slot.Opacity = animation.Opacity.Current;
  return animation.Opacity.Finished();
}
DrawImageCommand DrawImageCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  DrawImageCommand result;
  result.Route = reader.U32();
  result.Category = reader.U32();
  result.TextureExpression = reader.String();
  result.Slot = reader.U32();
  if (result.Category == 2)
    for (auto& value : result.Geometry) value = reader.I32();
  result.Bucket = reader.U32();
  result.From = reader.I32();
  result.To = reader.I32();
  result.Duration = reader.I32();
  result.Scale = reader.I32();
  result.Owner = reader.U32();
  result.Lane = reader.U32();
  ValidateSlot(result);
  if (result.Route && result.Lane >= 5)
    throw FormatError("image lane outside native range");
  return result;  // Serialized record alignment tail is not a field.
}
ReleaseImageCommand ReleaseImageCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  return {reader.U32(), reader.U32(), reader.I32()};
}
void StartReleaseImage(SemanticState& state, const ReleaseImageCommand& command,
                       SceneImageDriver& images) {
  auto id = command.IdOrIndex;
  if (command.Source == 1)
    id = state.Bank0.at(size_t(id));
  else if (command.Source != 0)
    id = state.Bank1.at(size_t(id));
  if (id < 0) return;  // Native warning only; no slot/resource mutation.
  auto& scene = state.SceneImages;
  if (!scene.Known)
    throw UnsupportedCommand(
        "restored image slots/reference counters are missing");
  const uint32_t category = command.Category == 0   ? 0
                            : command.Category == 1 ? 1
                                                    : 2;
  if (size_t(id) >= scene.Slots.TextureReferences[category].size()) return;
  PollSceneImages(scene, images);
  if (scene.PendingIncrements[category][id])
    throw UnsupportedCommand(
        "releasing an outstanding native image job is not implemented");
  images.ReleaseAsset({ImageCategorySection(category), uint32_t(id)});
  scene.Slots.TextureReferences[category][id] = 0;
  scene.Requested[category][id] = 0;
}
void StartDrawImage(SemanticState& state, DrawImageAnimation& animation,
                    const DrawImageCommand& command, SceneImageDriver& images,
                    uint64_t now) {
  ValidateSlot(command);
  if (command.Route || command.Owner)
    throw UnsupportedCommand(
        "DrawImage lane/slot-owned transitions are not implemented");
  if (command.Duration < 0)
    throw FormatError("negative image transition duration");
  if (!state.SceneImages.Known || !state.Effects.Known)
    throw UnsupportedCommand(
        "restored image slots/reference counters or common gate are missing");
  const auto texture = EvaluateNumeric(state, command.TextureExpression);
  CheckResource(state.SceneImages, command.Category, texture);
  if (texture > INT16_MAX)
    throw FormatError("image texture ID exceeds native slot");
  if (command.Category == 1 && texture >= 0)
    ValidateCgHistory(state.CgHistory, texture);
  PollSceneImages(state.SceneImages, images);
  DrawImageAnimation next;
  next.Started = next.Initialized = true;
  next.Texture = texture;
  const auto oldScale = Scale(state.SceneImages, command);
  if (texture >= 0) {
    const ImageKey key{ImageCategorySection(command.Category),
                       uint32_t(texture)};
    const bool ready = images.AssetReady(key);
    if (texture != Texture(state.SceneImages, command) ||
        !(ready || state.SceneImages.Requested[command.Category][texture])) {
      Request(state.SceneImages, images, command.Category, uint32_t(texture));
      if (!ready) next.Initialized = false;
    }
    if (command.Category == 0) state.LastBackgroundId = texture;
    if (command.Category == 1) MarkCgViewed(state, texture);
  }
  if (command.Category == 2) {
    auto& slot = state.SceneImages.Slots.IntegerSlots[command.Slot];
    slot.Flag = 0;
    for (size_t i = 0; i < 6; ++i)
      slot.GeometryAndTexture[i] =
          std::bit_cast<int16_t>(uint16_t(command.Geometry[i]));
    slot.GeometryAndTexture[6] = int16_t(texture);
    slot.Parameter6 = uint8_t(command.Bucket);
  } else {
    auto& slot =
        state.SceneImages.Slots.FloatSlots[command.Category][command.Slot];
    slot.Flag = 0;
    slot.Texture = int16_t(texture);
    slot.Parameters[2] = uint8_t(command.Bucket);
  }
  SetValues(state.SceneImages, command, command.From, command.Scale);
  next.Opacity.Start(command.From, command.To, command.Duration, now);
  next.Scale.Start(oldScale, command.Scale, command.Duration, now);
  animation = next;
}
bool UpdateDrawImage(SemanticState& state, DrawImageAnimation& animation,
                     const DrawImageCommand& command, SceneImageDriver& images,
                     bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand("DrawImage Start was suppressed");
  if (!images.Ready()) return false;  // Common native resource gate.
  PollSceneImages(state.SceneImages, images);
  const auto id = Texture(state.SceneImages, command);
  if (id == -1) {
    if (skipping) state.SkipPhase = 1;
    return true;
  }
  CheckResource(state.SceneImages, command.Category, id);
  const bool ready =
      images.AssetReady({ImageCategorySection(command.Category), uint32_t(id)});
  if (skipping) {
    state.SkipPhase = ready ? 1 : 2;
    if (!ready) return false;
    SetValues(state.SceneImages, command, command.To, command.Scale);
    return true;
  }
  if (!animation.Initialized) {
    if (!ready) return false;
    animation.Opacity.Start(command.From, command.To, command.Duration, now);
    animation.Scale.Start(Scale(state.SceneImages, command), command.Scale,
                          command.Duration, now);
    animation.Initialized = true;
  }
  animation.Opacity.Tick(now);
  animation.Scale.Tick(now);
  SetValues(state.SceneImages, command, animation.Opacity.Current,
            animation.Scale.Current);
  return animation.Opacity.Finished() && animation.Scale.Finished();
}
}  // namespace Impacto::Saekano
