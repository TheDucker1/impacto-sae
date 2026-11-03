#pragma once
#include "titlecontroller.h"

namespace Impacto::Saekano {
struct TitleDrawInputs {
  int32_t Phase=0,Selected=0,Character=0;
  bool ResourceDrawBlocked=false;
  int32_t Reveal=0,LogoMove=0,LogoDetail=0,MenuMove=0,MenuDetail=0;
  std::array<int32_t,4> Buttons{};
  int32_t BlinkAlpha=0;
  std::array<int32_t,2> AmbientDegrees{};
  uint32_t FontSize=27;
};
enum class TitleDrawKind { Clear,Blend,TransformReset,Sprite,Text,Widget,Effect };
struct TitleDrawCommand {
  TitleDrawKind Kind=TitleDrawKind::Sprite;
  UiSpriteBlit Sprite{};
  UiTextBlit Text{};
  uint32_t Index=0;
};
TitleDrawInputs TitleDrawFromState(const TitleState& state,int32_t blinkAlpha,
                                  std::array<int32_t,2> ambientDegrees,uint32_t fontSize);
// Main draw3612c only; overlay35fd6 uses BuildTitleOverlay below.
// Widget/effect internals and rasterization remain frontend consumers.
std::vector<TitleDrawCommand> BuildTitleDraw(const TitleEnvironment& env,const TitleDrawInputs& inputs);

enum class TitleOverlayKind { Resource,SharedDialog,Rectangle,Submenu,MovieChannel };
struct TitleOverlayInputs {
  int32_t Phase=0,FadeAlpha=0,MovieFadeAlpha=0;
  bool ResourceDrawBlocked=false,ResourceOverlayVisible=false;
  std::array<bool,3> SubmenusPresent{}; // TitleSubmenu ordering.
};
struct TitleOverlayCommand {
  TitleOverlayKind Kind=TitleOverlayKind::Rectangle;
  uint32_t Index=0;
  std::array<int32_t,4> Rectangle{};
  std::array<int32_t,2> Center{};
  std::array<uint8_t,4> Color{};
};
// Native35fd6, after the main pass. SharedDialog is the existing modal owner,
// centered at480/272 with a white90-alpha shade; inactive owners draw nothing.
std::vector<TitleOverlayCommand> BuildTitleOverlay(const TitleEnvironment& env,const TitleOverlayInputs& inputs);

struct TitleVisualState {
  // Native integer stepper: current/from/to/step/substep, sampled per update.
  std::array<int32_t,5> Blink{};
  bool BlinkDirection=true;
  std::array<ScriptLinearTween,2> Ambient;
};
// Constructor ambient channels and phase0 blink initialization.
void StartTitleVisuals(TitleVisualState& state,const TitleEnvironment& env,uint64_t now);
void UpdateTitlePressStartVisual(TitleVisualState& state);
void TickTitleAmbient(TitleVisualState& state,uint64_t now);
}
