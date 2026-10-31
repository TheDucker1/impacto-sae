#pragma once
#include "optionsnavigation.h"

namespace Impacto::Saekano {
// Native9f8d0/9f880: one integer step per update, not a millisecond tween.
struct SlotFrameChannel {
  int32_t Current=0,From=0,To=0,Frames=1,Elapsed=0;
  void Start(int32_t from,int32_t to,int32_t frames);
  void Tick();
};
struct SlotBrowserSettings {
  int32_t Pitch=0,ScrollFrames=0,PageStep=0,PageFrames=0;
  int32_t HighlightTo=0,HighlightDuration=0;
};
struct SlotBrowserState {
  OptionsRowNavigation Rows;
  OptionsNavigationState Navigation;
  SlotFrameChannel Scroll,Cursor;
  ScriptReversingTween Highlight;
  bool Started=false,RenderEnabled=true,KeyboardEnabled=true,Scrolling=false;
  std::array<bool,2> PageActivated{};
  std::array<int32_t,2> PageCounters{};
  SlotBrowserSettings Settings;
};
enum class SlotBrowserWidget { Page,Protect,Activate,Back };
struct SlotBrowserInput { OptionsNavigationInput Vertical;bool Protect=false,Accept=false,Cancel=false; };
struct SlotBrowserServices {
  virtual ~SlotBrowserServices()=default;
  virtual void UpdateWidget(SlotBrowserWidget kind,uint32_t row)=0;
  virtual bool PageFlick()=0; // Native group9c556: enabled component flag0x80.
  virtual float PageVelocityY()=0; // Native group9c936: release velocity Y.
  virtual bool Released(SlotBrowserWidget kind,uint32_t row)=0;
  virtual void UiSound(int32_t id)=0;
};
void StartSlotBrowser(SlotBrowserState& state,const SlotBrowserSettings& settings,
    std::span<const int32_t> repeatIntervals,int32_t selected,uint64_t now);
void SelectSlotBrowserRow(SlotBrowserState& state,int32_t selected);
// Original5c892/20b40. Actual widget queries are deferred to their native branch.
// Occupied bytes come from the real60 records; mode1 is load.
int32_t UpdateSlotBrowser(SlotBrowserState& state,const SlotBrowserInput& input,
    SlotBrowserServices& services,std::span<const uint8_t,60> occupied,int32_t mode,uint64_t now);
}
