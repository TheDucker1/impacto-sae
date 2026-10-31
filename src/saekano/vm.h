#pragma once

#include "flow.h"
#include "profilemode.h"

#include <memory>

namespace Impacto::Saekano {
struct SkipAutoState;
struct SkipAutoInput;
struct HistoryEnvironment;
struct ScriptReadHistory;

struct MessageRuntimeContext {
  // These are native context fields that the original portable schema omitted.
  // Restored callers must supply them before executing text; fresh contexts use
  // constructor defaults. Unknown restored state must not be guessed from text.
  bool Known = true;
  uint32_t PreviousCompletionPolicy = 0;  // C+1598
  bool SplitDialogue = false;            // C+20ac
  int32_t Side0Character = 0;             // C+20b4
  std::optional<bool> KeywordSuppressed = false;  // S+7a4d4; SAS1 omitted it.
};

class ScriptSource {
 public:
  virtual ~ScriptSource() = default;
  virtual Bytes Load(int32_t scriptId) = 0;
};

// Rendering/audio/game-mode gates are an explicit boundary. A backend accepts
// only opcodes it implements. Unknown presentation is never silently completed.
class VmBackend {
 public:
  virtual ~VmBackend() = default;
  virtual void ResetParticles(bool) {}
  virtual bool StartParticleEffect(const GameEvent&) { return false; }
  virtual void SetSplitDialogue(bool) {}
  virtual void SetMessageDualCleanup(bool) {}
  virtual void ClearMessage(size_t) {}
  virtual void BeginMessage(size_t command, const MessageCommand& message,
                             size_t context, uint64_t now) = 0;
  virtual bool MessageRevealed(size_t command, uint64_t now) = 0;
  virtual bool MessageAdvanceAllowed(size_t command, uint64_t now) = 0;
  virtual void RevealMessage(size_t command) = 0;
  // Native prepared-voice early return precedes input/reveal/auto decisions.
  virtual bool BeginMessageUpdate(size_t, uint64_t, UpdateRoute) { return true; }
  virtual void CompleteMessage(size_t, uint64_t) {}
  // Native74210: completed foreground commands mark their exact segment before dispatch.
  virtual void RecordReadHistory(int32_t, uint32_t) {}
  virtual void RecordChoiceHistory(int32_t, uint32_t, std::span<const size_t>) {}
  // Normal message update tail, after reveal/input/auto decisions. Render-only
  // sampling and the next-update completion dispatch do not publish this flag.
  virtual void EndMessageUpdate(size_t, uint64_t) {}
  virtual Live2dDriver* Models() { return nullptr; }
  virtual SceneImageDriver* Images() { return nullptr; }
  virtual CharacterPoseDriver* Poses() { return nullptr; }
  virtual ApproachResources* ApproachAssets() { return nullptr; }
  virtual BattleResources* BattleAssets() { return nullptr; }
  virtual const BattleVersusEnvironment* BattleVersusConfig() const { return nullptr; }
  virtual const BattleSelectionEnvironment* BattleSelectionConfig() const { return nullptr; }
  virtual BattleSelectionHost* BattleSelectionConsumer() { return nullptr; }
  virtual const BattleResultEnvironment* BattleResultConfig() const { return nullptr; }
  virtual BattleResultHost* BattleResultConsumer() { return nullptr; }
  virtual SkipAutoState* AutoPolicy() { return nullptr; }
  virtual ApproachMissionAudio* MissionVoices() { return nullptr; }
  virtual const ApproachMissionEnvironment* MissionConfig() const { return nullptr; }
  virtual const ApproachSelectionEnvironment* SelectionConfig() const { return nullptr; }
  virtual const ApproachResultEnvironment* ApproachResultConfig() const { return nullptr; }
  virtual ApproachResultHost* ResultHost() { return nullptr; }
  virtual BgmResources* Music() { return nullptr; }
  virtual SeResources* SoundEffects() { return nullptr; }
  virtual const BattleGaugeEnvironment* BattleEnvironment() const { return nullptr; }
  virtual const ApproachGaugeEnvironment* ApproachEnvironment() const { return nullptr; }
  virtual const ChapterEnvironment* ChapterConfig() const { return nullptr; }
  virtual const ProfileEnvironment* ProfileConfig() const { return nullptr; }
  virtual const ModeResultEnvironment* ModeResultConfig() const { return nullptr; }
  virtual ProfileServices* ProfileHost() { return nullptr; }
  virtual bool Supports(uint16_t) const { return false; }
  virtual CompletedCommand Route(uint16_t opcode, std::span<const uint8_t> payload) const;
  virtual void StartPresentation(size_t command, uint16_t opcode,
                                  std::span<const uint8_t> payload, bool lane, uint64_t now);
  virtual bool UpdatePresentation(size_t command, bool lane, UpdateRoute route, uint64_t now);
};

enum class VmBoundary {
  BeforeStart, CommandStarted, WaitingInput, WaitingBackend,
  CommandCompletedBeforeDispatch, FileLoadedBeforeStart, ProfileDestinationLoaded, Ended
};
struct VmEvent {
  VmBoundary Boundary;
  int32_t Script;
  int32_t Command;
  uint16_t Opcode;
};

// Owns decoded script bytes and one mutable runtime object per authored command.
// Each Step exposes a real lifecycle boundary, allowing observers to stop before
// dispatch without reassigning the PC. Callers provide clock and input events.
class VirtualMachine {
 public:
  VirtualMachine(ScriptSource& scripts, VmBackend& backend, SemanticState initial,
                  MessageRuntimeContext messageContext = {});
  // New outer scene2 from the native title. Requires working session reset;
  // constructs a fresh manager at actual entry0/0, with global progress intact.
  static std::unique_ptr<VirtualMachine> FromTitleNewGame(
      ScriptSource& scripts,VmBackend& backend,SemanticState owner,
      MessageRuntimeContext messageContext={});
  // Outer-loop frontend handoff after a standalone ProfileMode completes.
  // Uses the same fresh-manager reset as the opcode3ed owning path. Destination
  // is derived from the completed mode; callers retain their audio/backend
  // host.
  static std::unique_ptr<VirtualMachine> FromCompletedProfile(
      ScriptSource& scripts, VmBackend& backend, SemanticState owner,
      const ProfileMode& completed, MessageRuntimeContext messageContext = {});
  const SemanticState& State() const { return state_; }
  const DispatchState& Dispatcher() const { return dispatcher_; }
  VmBoundary Boundary() const { return boundary_; }
  uint16_t Opcode() const;
  const QuakeAnimation* CurrentQuake() const;
  const ChapterTitleAnimation* CurrentChapter() const;
  std::span<const ApproachEntry> CurrentApproachEntries() const;
  const ProfileMode* CurrentProfile() const { return profile_.get(); }
  VmEvent Step(uint64_t now, const CommandInput& input = {});
  void UpdateSkipAuto(SkipAutoState& policy, const SkipAutoInput& input,
                      int32_t autoKeyDuration, uint64_t now);
  bool SkipHistoryGate(const HistoryEnvironment& environment,
                       const ScriptReadHistory* currentFileHistory) const;
  void SetGauge(bool battle, int32_t value);
  void ActivateLane(size_t lane);
  std::span<const size_t> QueuedLaneCommands(size_t lane) const { return dispatcher_.LaneQueues.at(lane); }
  void RequestLaneFlush() { dispatcher_.FlushLanes = true; }
  void SetMessageContext(MessageRuntimeContext context);
  void SeedRandom(uint32_t seed);
  // Frontend calls once per real frame, after VM work, with its current clock.
  // Fresh managers may create controllers; arbitrary restored schedules reject.
  void UpdateSpriteFrames(uint32_t clockSeed);
  const CharacterBlink* SpriteBlink(uint16_t id) const;
  std::vector<GameEvent> TakeEvents();

 private:
  VirtualMachine(ScriptSource& scripts, VmBackend& backend,
                 SemanticState&& initial, MessageRuntimeContext messageContext,
                 bool replaceManager);
  struct RuntimeCommand {
    bool Started = false;
    bool LaneStarted = false;
    bool AdvancePending = false;
    uint64_t Epoch = 0;
    uint64_t LaneEpoch = 0;
    uint32_t SavedDay = 0;
    CompletedCommand Routing;
    std::optional<MessageCommand> Message;
    std::optional<std::vector<ApproachEntry>> ApproachEntries;
    std::optional<PlayBgmCommand> PlayBgm;
    std::optional<PlaySeCommand> PlaySe;
    std::optional<StopSeCommand> StopSe;
    std::optional<ChapterTitleCommand> Chapter;
    ChapterTitleAnimation ChapterAnimation;
    size_t MessageSide = 0;
    std::optional<Live2dCommand> Live2d;
    std::optional<LipActionCommand> LipAction;
    std::optional<BattleEntryCommand> BattleEntry;
    std::optional<BattleVersusCommand> BattleVersus;
    std::optional<BattleGaugeCommand> BattleGauge;
    BattleGaugeAnimation BattleAnimation;
    std::optional<ApproachGaugeCommand> ApproachGauge;
    ApproachGaugeAnimation ApproachAnimation;
    std::optional<RuleWipeCommand> RuleWipe;
    RuleWipeAnimation WipeAnimation;
    std::optional<QuakeCommand> Quake;
    QuakeAnimation QuakeRuntime;
    std::optional<ScreenFadeCommand> ScreenFade;
    ScreenFadeAnimation FadeAnimation;
    std::optional<ScreenFilterCommand> ScreenFilter;
    ScreenFilterAnimation FilterAnimation;
    std::optional<DrawImageCommand> DrawImage;
    std::optional<FillRectCommand> FillRect;
    FillRectAnimation FillAnimation;
    DrawImageAnimation ImageAnimation;
    std::optional<CharacterColorCommand> CharacterColor;
    std::optional<CharacterPositionCommand> CharacterPosition;
    std::optional<CharacterOffsetCommand> CharacterOffset;
    CharacterOffsetAnimation OffsetAnimation;
    std::optional<CharacterMoveCommand> CharacterMove;
    CharacterMoveAnimation MoveAnimation;
    std::optional<CharacterDrawCommand> CharacterDraw;
    CharacterDrawAnimation DrawAnimation;
    std::optional<CharacterPoseCommand> CharacterPose;
    std::optional<CharacterReserveCommand> CharacterReserve;
    CharacterPoseAnimation PoseAnimation;
    std::optional<CharacterFaceCommand> CharacterFace;
    CharacterFaceAnimation FaceAnimation;
    std::optional<CharacterScaleCommand> CharacterScale;
    CharacterScaleAnimation ScaleAnimation;
    int32_t WaitDuration = 0;
    uint32_t WaitHonorSkip = 0;
    size_t SelectedOrdinal = 0;
    uint32_t BattleDuration = 0;
    uint32_t WindowDuration = 0;
    uint32_t WindowChannel = 0;
  };
  ScriptSource& scripts_;
  VmBackend& backend_;
  SemanticState state_;
  MessageRuntimeContext messageContext_;
  DispatchState dispatcher_;
  std::unique_ptr<ScriptObject> script_;
  std::vector<RuntimeCommand> commands_;
  std::array<std::vector<int32_t>, 2> messageKeywords_;
  VmBoundary boundary_ = VmBoundary::BeforeStart;
  std::unique_ptr<ProfileMode> profile_;
  std::map<uint16_t, CharacterBlink> spriteBlinks_;
  bool freshSpriteFrames_ = false;
  void Load(bool preserveOwnerState);
  void Start(size_t index, uint64_t now, bool lane);
  bool Update(size_t index, uint64_t now, const CommandInput& input, bool lane);
  void SetupMessage(size_t index, uint64_t now);
  void CollectMessageKeywords(size_t index);
  void UpdateLanes(uint64_t now);
  VmEvent Event(VmBoundary boundary) const;
  VmEvent FrameEvent(VmBoundary boundary, uint64_t now);
};

}  // namespace Impacto::Saekano
