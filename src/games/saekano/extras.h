#pragma once
#include "album.h"
#include "battlewidgets.h"
#include "optionsnavigation.h"
#include "titlecontroller.h"
#include "music.h"
#include <functional>

namespace Impacto::Saekano {
enum class ExtrasSection : int32_t { Album,Movies,Music,Battle };
struct ExtrasInput { OptionsNavigationInput Vertical;bool Accept=false,Cancel=false; };
// These are the native owned child/dialog/save boundaries. Implementations must
// provide actual owners; a selected child is never replaced by a fixed result.
struct ExtrasServices {
  virtual ~ExtrasServices()=default;
  virtual void UiSound(int32_t id)=0;
  virtual void SetExtrasAudio(bool active)=0;
  virtual void BeginChild(ExtrasSection section)=0;
  virtual int32_t UpdateChild()=0;
  virtual bool ChildDirty()const=0;
  virtual void DestroyChild()noexcept=0;
  virtual void BeginSaveQuestion(const std::u16string& prompt)=0;
  virtual OptionsDialogPoll UpdateSaveQuestion()=0;
  virtual void BeginSystemSave()=0;
  virtual OptionsSavePoll UpdateSystemSave()=0;
};
// Native45878/45c72/45d74, composed from the existing shared list, widgets,
// fade, history and child/save services. Presentation is a separate consumer.
class ExtrasRoot {
 public:
  ExtrasRoot(AlbumEnvironment environment,std::span<const int32_t> repeats,
             uint32_t holdFrames,MusicHistoryState& history,ExtrasServices& services,
             int32_t initialSection=-1);
  ~ExtrasRoot(){if(child_)services_.DestroyChild();}
  ExtrasRoot(const ExtrasRoot&)=delete;
  ExtrasRoot& operator=(const ExtrasRoot&)=delete;
  void BeginFrame(WidgetInputHistory::Snapshot contacts){contacts_.BeginFrame(std::move(contacts));}
  void CancelPointer();
  int32_t Update(const ExtrasInput& input,uint64_t now); //0 owned,2 closed.
  int32_t Phase()const{return phase_;}
  int32_t Selected()const{return rows_.Selected;}
  int32_t Section()const{return section_;}
  int32_t Fade()const{return fade_.Value.Current;}
  bool ChildPresent()const{return child_;}
  bool ControlsConfigured()const{return !buttons_.empty();}
  const AlbumEnvironment& Environment()const{return env_;}
 private:
  AlbumEnvironment env_;ExtrasServices& services_;MusicHistoryState& history_;
  OptionsNavigationState navigation_;OptionsRowNavigation rows_;
  WidgetInputHistory contacts_;
  std::vector<BattleCardSprite> buttons_;
  std::array<std::vector<BattleCardSprite>,4> regions_;
  BattleCardSprite cancel_;
  TitleTween fade_;
  int32_t phase_=0,section_=-1;
  std::optional<int32_t> pending_;
  bool child_=false,closed_=false;
  void Sample(BattleCardSprite& widget,std::array<int32_t,2> offset,uint64_t now);
  bool Select(const ExtrasInput& input,uint64_t now);
};
enum class ExtrasDrawKind { BeginMask,Multiply,EndMask,Sprite,Mask,Child,Help,Text,Fade,Overlays };
struct ExtrasDrawCommand {
  ExtrasDrawKind Kind=ExtrasDrawKind::Sprite;
  UiSpriteBlit Sprite;
  UiTextBlit Text;
};
//46a94 lowering. BeginMask clears a960x544 target to opaque black; Multiply
// selects the already-established blend entry3. Mask samples that real target.
std::vector<ExtrasDrawCommand> BuildExtrasRootDraw(const ExtrasRoot& root,uint32_t utilitySize,
    const std::function<int32_t(std::u16string_view,uint32_t)>& measure);
}
