#pragma once
#include "systemmenuenv.h"
#include "scenariomenu.h"
#include "titlecontroller.h"
#include "battleselection.h"
#include "battlewidgets.h"
#include "options.h"
#include <variant>

namespace Impacto::Saekano {
struct SystemMenuServices {
  virtual ~SystemMenuServices() = default;
  virtual void UiSound(int32_t id) = 0;
  // Existing dialog/save owners provide actual measured layout, input and I/O.
  virtual void BeginQuestion(const std::u16string& prompt, uint32_t buttonGroup) = 0;
  virtual OptionsDialogPoll PollQuestion() = 0;
  virtual void BeginSystemSave() = 0;
  virtual OptionsSavePoll PollSystemSave() = 0;
  virtual void RetireQuestion() = 0;
};
using SystemMenuDraw = std::variant<UiRenderCommand, UiTextBlit>;

// Native5de88/5e83e/5e408/5ee84/5f0ce. Contains the existing native widget,
// linear tween and cubic motion primitives rather than a second UI backend.
class SystemMenu final : private SystemMenuInputServices {
 public:
  SystemMenu(SystemMenuEnvironment environment, ProfileTextureSection textureBank,
             uint32_t holdFrames, SystemMenuServices& services, int32_t initialSelection=0);
  void BeginFrame(WidgetInputHistory::Snapshot contacts);
  void CancelPointer();
  // Returns0 while owned, otherwise1..5 selected action or-1 dismiss. Action5
  // returns only after the real return-to-title owner completes.
  int32_t Update(SystemMenuInput keys, uint64_t now);
  std::vector<SystemMenuDraw> Draw(int32_t chapterId,
      const std::u16string& chapterText, uint32_t utilityFontSize) const;
  int32_t Phase() const { return phase_; }
  int32_t Selected() const { return selected_; }

 private:
  struct Row {
    std::array<BattleCardSprite,2> Variants;
    TitleTween Opacity, X;
    size_t Variant = 1;
  };
  SystemMenuEnvironment env_;
  ProfileTextureSection bank_;
  SystemMenuServices& services_;
  WidgetInputHistory contacts_;
  std::vector<Row> rows_;
  BattleCardSprite close_, back_, backContact_;
  TitleTween fade_, headerOpacity_, headerY_, lineOpacity_, selectionOpacity_;
  TitleTween selectionWidth_, selectionSweep_, chapterOpacity_;
  TitleTween backY_, closeX_, closeOpacity_, indicator_;
  BattleCardMotion selectionScale_;
  int32_t phase_ = 0, entryPhase_ = 0, backPhase_ = 0, nextRow_ = 0;
  int32_t selected_ = 0, action_ = 0;
  int32_t returnPhase_ = 0;
  std::optional<int32_t> pendingReturnPhase_;
  std::optional<int32_t> pendingPhase_;
  uint64_t entryEpoch_ = 0, now_ = 0;
  bool sweepStarted_ = false;
  int32_t Word(uint16_t offset) const;
  ProfileTextureKey Texture(uint32_t index) const { return {bank_, -1, index}; }
  void Setup(uint64_t now);
  bool AnimateEntry(uint64_t now);
  void UpdateWidgets(uint64_t now);
  int32_t UpdateReturnToTitle();
  void UiSound(int32_t id) override { services_.UiSound(id); }
  void Highlight(int32_t row, bool enabled) override;
  void RestartSelectionEffects(int32_t row) override;
};
}
