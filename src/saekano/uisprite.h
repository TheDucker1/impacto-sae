#pragma once

#include "profiletexture.h"

namespace Impacto::Saekano {
class PayloadReader;
struct UiSpriteDefinition {
  int32_t Parameter = 0, Alignment = 0, X = 0, Y = 0;
  int32_t SourceX = 0, SourceY = 0, Width = 0, Height = 0;
  std::array<uint8_t, 4> Color{};
  static UiSpriteDefinition Read(PayloadReader& reader);
  static UiSpriteDefinition ReadLegacy(PayloadReader& reader);
};
struct UiSpriteBlit {
  ProfileTextureKey Texture;
  std::array<int32_t, 4> Destination{}, Source{};  // x,y,width,height.
  std::array<uint8_t, 4> Color{};
};
// Native8017c4e0 lowers its scale/alignment to the raw sprite backend call.
UiSpriteBlit ResolveUiSprite(ProfileTextureKey texture,
                             const UiSpriteDefinition& sprite, float scaleX = 1,
                             float scaleY = 1);
struct ProfileBackdropEnvironment {
  int32_t IntroFade = 0, WavePeriod = 0;
  std::array<UiSpriteDefinition, 3> Sprites;
  size_t PrefixBytes = 0;
  static ProfileBackdropEnvironment Decode(std::span<const uint8_t> bytes);
};
struct ProfileMenuChromeEnvironment {
  UiSpriteDefinition Frame, Header, Stretch, EarlyOverlay, LateOverlay, Footer,
      Marker;
  std::array<int32_t, 2> StretchSize{}, MarkerStep{};
  std::vector<std::array<int32_t, 2>> MarkerPositions;
};
// Native500ac..50372, after backdrop and before stencil rendering.
std::vector<UiSpriteBlit> BuildProfileMenuChrome(
    const ProfileMenuChromeEnvironment& environment, int32_t phase,
    int32_t resultSlide);
enum class UiRenderKind {
  ResetDepthStencilBackground,
  Stencil,
  AlphaTest,
  DepthTest,
  Rectangle,
  Sprite
};
struct UiRenderCommand {
  UiRenderKind Kind = UiRenderKind::ResetDepthStencilBackground;
  int32_t Mode = 0;
  std::array<int32_t, 4> Rectangle{};
  std::array<uint8_t, 4> Color{};
  UiSpriteBlit Sprite;
};
struct ModeResultVisualEnvironment {
  UiSpriteDefinition Banner, ProgressFill, Frame, MaskedContent, Mask, EventIcon;
  std::array<UiSpriteDefinition, 3> Outcomes;
  std::array<int32_t, 2> TextPosition{};
  int32_t TextWidth = 0;
  float TextScale = 0;
  std::array<std::array<int32_t, 2>, 2> CursorPositions{};
  int32_t OverlayAlpha = 0;
  std::vector<std::array<uint8_t, 4>> CharacterColors;
  size_t PrefixBytes = 0;
  static ModeResultVisualEnvironment DecodeApproach(
      std::span<const uint8_t> bytes);
};
struct ProfileResultState;
// Native50b1a..510be: dim/reflection/banner, masked cut-in, event image and
// completion fill. Text, cursor and final outcome draw follow this pass.
std::vector<UiRenderCommand> BuildProfileResultLayers(
    const ModeResultVisualEnvironment& environment,
    const UiSpriteDefinition& reflection, const ProfileResultState& result,
    int32_t canvasWidth, int32_t canvasHeight);
struct MessageCursorEnvironment {
  std::array<UiSpriteDefinition, 2> Sprites;
  size_t PrefixBytes = 0;
  static MessageCursorEnvironment Decode(std::span<const uint8_t> message);
};
struct OrdinaryMessageEnvironment {
  std::array<UiSpriteDefinition, 2> Frame;
  std::array<UiSpriteDefinition, 3> Nameplate;
  std::array<UiSpriteDefinition, 2> Cursors;
  std::array<UiSpriteDefinition, 9> Auxiliary;
  int32_t WindowTravel = 0, TextWidth = 0;
  std::array<int32_t, 2> TextPosition{};
  size_t PrefixBytes = 0;
  static OrdinaryMessageEnvironment Decode(std::span<const uint8_t> message);
};
struct OrdinaryMessageWindowState {
  uint32_t Openness = 0;
  uint8_t OpacitySetting = 0;  // Owner6022f, native slider0..11.
  std::array<int32_t, 2> Quake{};
  int32_t SpeakerId = -1;  // Manager15a4 = context159c, distinct from BGM.
  bool MessageCommand = false, CursorVisible = false;  // Manager1668 gate.
  float CursorOffset = 0;  // Manager1670 sample.
  uint32_t RendererMode = 0, RendererParameter = 0;  // Parameter is an F32 bit pattern.
};
enum class MessageWindowDrawKind { Sprite, Widget, RendererState, AlphaTest };
struct MessageWindowDraw {
  MessageWindowDrawKind Kind = MessageWindowDrawKind::Sprite;
  UiSpriteBlit Sprite;
  uint32_t Index = 0;      // Widget:manager20fc/15078/15050; Renderer:mode.
  uint32_t Parameter = 0;  // Widget:alpha; Renderer:native F32 parameter bits.
  std::array<int32_t, 2>
      Offset{};  // Widget frame offset, separate from authored geometry.
};
// Native79a9c ordered frame/nameplate/cursor sprites and text widget calls.
// Generic widget bodies and renderer mode backend execution are separate.
std::vector<MessageWindowDraw> BuildOrdinaryMessageWindow(
    const OrdinaryMessageEnvironment& environment,
    const OrdinaryMessageWindowState& state);
// Static ordinary skip/auto widget rows from BindScenarioOwner. Input policy
// and active-auto rotation are separate controllers; active auto rejects here.
std::vector<UiSpriteBlit> BuildOrdinaryMessageWidget(
    const OrdinaryMessageEnvironment& environment, uint32_t widget,
    bool skipActive, bool skipGesture, bool autoActive,
    std::array<int32_t, 2> offset, uint8_t alpha);
struct ProfileResponseVisualEnvironment {
  UiSpriteDefinition Window;
  std::array<int32_t, 2> TextPosition{};
  int32_t TextWidth = 0;
  float TextScale = 0;
  std::array<UiSpriteDefinition, 2> Cursors;
  size_t PrefixBytes = 0;
  static ProfileResponseVisualEnvironment Decode(std::span<const uint8_t> profile);
};
struct ProfileResponseUiDraws {
  UiSpriteBlit Window;
  std::vector<UiSpriteBlit> Cursors;
};
// Native51504..517f4, after background/actor. Caller draws response glyphs
// between Window and Cursors and supplies the actual response reveal state.
ProfileResponseUiDraws BuildProfileResponseUi(
    const ProfileResponseVisualEnvironment& environment, bool textRevealed,
    float cursorOffset);
// Native510e0..5148a, after text: revealed/finished gate, two cursor hints
// using the current float-channel sample, then the category outcome icon.
std::vector<UiSpriteBlit> BuildProfileResultOverlays(
    const ModeResultVisualEnvironment& environment,
    const MessageCursorEnvironment& cursors, const ProfileResultState& result,
    bool textRevealed, float cursorOffset);
struct ProfileItemFrameEnvironment {
  UiSpriteDefinition Sprite;
  std::vector<std::array<int32_t, 2>> Positions;
};
struct ProfileItemContentEnvironment {
  UiSpriteDefinition Selector;
  std::vector<UiSpriteDefinition> Categories;
  std::array<int32_t, 2> LabelOffset{};
  float LabelScale = 0;
};
struct ProfileArrowEnvironment {
  std::array<UiSpriteDefinition, 2> Upper, Lower;
};
struct ProfileNavigationEnvironment {
  std::array<UiSpriteDefinition, 2> Sprites;
  float Travel = 0;
  int32_t Duration = 0;
};
struct ProfilePortraitEnvironment {
  UiSpriteDefinition Sprite;
  std::array<int32_t, 4> Area{};
};
struct ProfileHudEnvironment {
  UiSpriteDefinition BeforePortrait;
  std::array<UiSpriteDefinition, 4> AfterPortrait;
  UiSpriteDefinition StatBar, CharacterName;
  std::vector<std::array<uint8_t, 4>> StatColors;
};
// Native800bf450..5b4: signed starts/destination ends, unsigned source ends.
std::array<std::array<float, 4>, 4> ResolveUiQuad(
    const UiSpriteBlit& blit, uint32_t textureWidth, uint32_t textureHeight);
struct UiTextBlit {
  std::u16string Text;
  std::array<int32_t, 2> Position{};
  uint32_t Size = 0;
  std::array<uint8_t, 4> Color{};
};
// Native50372..50472, with the factory's initial eight widget positions.
// Subsequent carousel movement must supply its updated widget geometry.
std::vector<UiRenderCommand> BuildProfileItemFramePass(
    const ProfileItemFrameEnvironment& environment, int32_t phase,
    int32_t canvasWidth, int32_t canvasHeight,
    std::span<const std::array<int32_t, 2>> frameOffsets = {});
// Native4ff68..500a8. Caller supplies actual animation state, never an oracle.
std::vector<UiSpriteBlit> BuildProfileBackdrop(
    const ProfileBackdropEnvironment& environment, int32_t waveOffset);
}  // namespace Impacto::Saekano
