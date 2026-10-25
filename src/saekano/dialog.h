#pragma once
#include "formats.h"
#include "uisprite.h"
#include "font.h"
#include "gauge.h"
#include <array>
#include <string>

namespace Impacto::Saekano {
struct DialogButtonLabels {
  std::u16string Primary, Secondary;
};
// env_dialog reader801cbccc..801cd1ca. Rect24 fields are x,y,sourceX,
// sourceY,width,height, without sprite alignment/color words.
struct DialogEnvironment {
  std::array<uint8_t, 3> Flags{};
  std::array<int32_t, 2> HeaderWords{};
  std::vector<std::array<int32_t, 6>> FrameParts;
  std::array<int32_t, 6> Focus{};
  std::array<std::vector<std::array<int32_t, 2>>, 3> ButtonPositions;
  std::array<std::array<int32_t, 2>, 2> Vectors{};
  std::array<std::array<int32_t, 6>, 2> Rectangles{};
  std::array<int32_t, 2> Offset{};
  std::array<int32_t, 6> Decoration{};
  std::array<int32_t, 2> TextExtent{};
  // c438..c45c, retaining raw words until each consumer is traced.
  std::array<int32_t, 10> Settings{};
  std::array<float, 2> TextScales{};
  std::u16string Separator;
  std::array<std::array<uint8_t, 4>, 4> Colors{};
  std::vector<std::vector<DialogButtonLabels>> ButtonGroups;
  std::array<uint8_t, 4> FinalColor{};
  size_t PrefixBytes = 0;
  static DialogEnvironment Decode(std::span<const uint8_t> bytes);
};
// Separate tail reader: older profile timing fixtures intentionally end before
// this prompt. Reading a shorter fixture never fabricates a confirmation
// string.
std::u16string DecodeProfileConfirmationPrompt(std::span<const uint8_t> bytes);
struct DialogFrameDraws {
  std::array<int32_t, 2> Origin{}, Extent{};
  UiRenderCommand Overlay;
  std::vector<UiSpriteBlit> Tiles;
};
struct DialogContentLayout {
  int32_t Width = 0, ContentHeight = 0, BodyHeight = 0;
  int32_t OpenDuration = 0, CloseDuration = 0;
  std::array<int32_t, 2> PromptPosition{};
  int32_t ButtonBaseline = 0;
};
struct DialogInput {
  bool Left = false, Right = false, Accept = false, Cancel = false;
  // Actual widget activation events, after the frontend hit-test service.
  std::array<bool, 3> Activated{};
};
struct DialogState {
  bool Visible = false, FocusEnabled = true, CancelEnabled = false,
       Canceled = false, CloseOnConfirm = false;
  int32_t Phase = 0, Selected = 0, Result = -1;
  std::optional<int32_t> PendingPhase;
  size_t ButtonCount = 0;
  int32_t OpenDuration = 0, CloseDuration = 0;
  ScriptLinearTween Opacity;
  std::vector<int32_t> UiSounds;
};
// Fresh modal setup plus native Open. Durations come from decoded layout.
void BeginDialog(DialogState& state, const DialogContentLayout& layout,
                 size_t buttons, int32_t initialSelection, bool cancelEnabled,
                 bool closeOnConfirm, uint64_t now);
void UpdateDialog(DialogState& state, const DialogInput& input, uint64_t now);
void CloseDialog(DialogState& state);
// Text's measured uint16 height is supplied by its layout service. This does
// not infer the prompt's glyph metrics or replace the native line formatter.
DialogContentLayout BuildDialogContentLayout(
    const DialogEnvironment& environment, std::array<int32_t, 2> center,
    uint16_t textHeight, size_t buttons, int32_t baseFontSize,
    int32_t openDuration = -1, int32_t closeDuration = -1);
struct DialogButtonDraws {
  std::vector<UiSpriteBlit> Background;
  UiTextBlit Label;
};
// Native8536..8988. Background layers precede each centered secondary label.
std::vector<DialogButtonDraws> BuildDialogButtons(
    const DialogEnvironment& environment, std::array<int32_t, 2> center,
    const DialogContentLayout& layout,
    std::span<const DialogButtonLabels> labels, int32_t selected,
    bool focusEnabled, int32_t progress, int32_t baseFontSize,
    const GsfFont& font, uint32_t nominalSize);
// Native81fc..8456: patterned fill, separator strip and corner ornament.
std::vector<UiSpriteBlit> BuildDialogDecoration(
    const DialogEnvironment& environment, const DialogFrameDraws& frame,
    int32_t progress);
// Native7cfc..81fc prefix. Rounded body height comes from the actual
// text/button layout; it is not guessed here. Text, buttons and focus follow
// this prefix.
DialogFrameDraws BuildDialogFrame(const DialogEnvironment& environment,
                                  std::array<int32_t, 2> center, int32_t width,
                                  int32_t bodyHeight, int32_t progress,
                                  std::array<uint8_t, 4> overlayColor);
}  // namespace Impacto::Saekano
