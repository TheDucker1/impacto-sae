#pragma once

#include "formats.h"
#include "contextsnapshot.h"
#include "workingcontext.h"
#include "backlog.h"
#include "systemsave.h"
#include "ordinaryvoice.h"
#include "live2dstate.h"
#include "gauge.h"
#include "random.h"
#include "transition.h"
#include "imagelayer.h"
#include "character.h"
#include "pose.h"
#include "music.h"
#include "se.h"
#include "chapter.h"
#include "profile.h"
#include "titlesession.h"
#include "approachentry.h"
#include "approachmission.h"
#include "approachselection.h"
#include "ordinarychoice.h"
#include "approachresult.h"
#include "mouthcontrol.h"
#include "battleentry.h"
#include "battleversus.h"
#include "battleselection.h"
#include "battleresult.h"
#include "particle.h"

#include <array>
#include <map>
#include <optional>
#include <string>

namespace Impacto::Saekano {

enum class GameEventKind { Sound, Effect, Trophy, StopBgm, StopChannel, PlayBgm, StopSe, PlaySe, PauseChannel, PumpAudio, SetBgmFadeDuration, PlayBgmRetainingGain, StopBgmWithCurrentFade };
struct GameEvent {
  GameEventKind Kind;
  int32_t Id;
  std::array<int32_t, 2> ScreenPosition{};
  int32_t Parameter = 0;  // StopBgm: signed milliseconds; StopSe mapping remains unbound.
  std::optional<BgmPlayRequest> Bgm{};
  std::optional<SePlayRequest> Se{};
};

struct SemanticState {
  BacklogHistory Backlog;  // Shared session owner; survives manager/file replacement.
  ContextSnapshotMetadata SnapshotMetadata;
  std::array<ScriptLinearTween,ParticleManager::GroupCount> ScriptParticleOpacity;
  std::optional<NativeWorkingRecord> WorkingSave;  // Session S5e814; absent in portable inputs.
  // Complete raw persistent owner retains all nine history groups, slot metadata,
  // unknown options/words. Typed gameplay consumers below overlay their fields.
  std::optional<NativeSystemSave> SystemProgress;
  // Ephemeral native owner6031c; system.bin does not serialize this byte.
  std::optional<uint8_t> OptionsHeaderByte;
  // C14514 / M1451c. Native context reset initializes this independent gate
  // to0; retain it separately from the approach-gauge-changing byte C1451c.
  bool ScenarioMenuBlocked = false;
  std::optional<OrdinaryVoiceSettings> VoiceSettings;
  int32_t ScriptFileId = 0;
  int32_t CurrentCommandIndex = 0;
  int32_t PendingCommandIndex = -1;
  int32_t PendingFileId = -2;
  int32_t SpeakerId = -1;
  std::u16string ActiveText;
  int32_t SkipMode = 0;
  int32_t SkipPhase = 0;
  int32_t ExitRequested = 0;
  int32_t DayEnabled = 0;
  int32_t ChapterId = 0;
  int32_t OtakuMode = 0;
  int32_t ApproachGauge = 0;
  int32_t BattleGauge = 0;
  int32_t SkipUnreadEnabled = 0;
  int32_t FirstAsyncCommandIndex = -1;
  std::array<int16_t, 1024> Bank0{};
  std::array<int16_t, 80> Bank1{};
  std::array<int32_t, 5> LaneCommandIndex{-1, -1, -1, -1, -1};
  std::array<uint8_t, 18> ApproachFlags{};
  std::array<int32_t, 2> TimeComponents{};
  std::vector<std::array<int32_t, 3>> DivergenceStack;
  std::array<std::vector<std::u16string>, 2> Messages;
  std::vector<std::u16string> Titles;
  std::optional<bool> TitleDisplay;  // C14515; omitted from portable restores.
  std::optional<ChapterTitleCommand> SavedChapter;  // S600b4/6007c/60044.
  std::optional<SavedDayTime> WorkingDayTime;  // S600f0/f4/f8, separate from visible day.
  ProfileOwnerState ProfileOwner;
  std::optional<uint8_t> TextSpeed;  // Owner S6022c, omitted from SAS1.
  std::optional<uint8_t> AutoSpeed;  // Owner S6022d, omitted from SAS1.
  std::optional<bool> ContinueAutoAfterSelection,ContinueSkipAfterSelection; // S60230/31.
  std::optional<uint8_t> WindowOpacity;  // Owner S6022f, omitted from SAS1.
  std::optional<bool>
      ReadColorEnabled;  // Owner S60232; read history is separate.
  std::map<uint16_t, CharacterState> Characters;
  CharacterRuntime CharacterRuntimeState;
  CharacterPoseCache PoseCache;
  std::array<uint32_t, 2> WindowOpenness{};
  std::array<uint32_t, 2> WindowTypes{};
  Live2dState Live2d;
  OrdinaryChoiceState OrdinaryChoice;
  std::optional<OrdinaryMouthControl> MouthControl; // C1c4c..1c57; omitted from portable restores.
  BattleGaugeState Battle;
  BattleEntryState BattleEntry;
  BattleVersusState BattleVersus;
  BattleSelectionState BattleSelection;
  BattleResultState BattleResult;
  MusicHistoryState BattleHistory; // S602dc vector: same native played-status predicate as music history.
  GameRandom Random;
  ApproachGaugeState Approach;
  ApproachEntryState ApproachMode;
  ApproachMissionState ApproachMission;
  ApproachSelectionState ApproachSelection;
  ApproachResultState ApproachResult;
  std::optional<int32_t> BgmTrack;  // C+15a4; absent from the portable restore schema.
  std::optional<int32_t> BgmCommandVolume;  // C+15a8.
  std::optional<int32_t> BgmMasterVolume;  // Service master setting, percentage.
  std::optional<int32_t> SeTrack, SeCommandVolume, SeChannel;  // C15ac/b0/b4.
  std::optional<int32_t> SeMasterVolume;  // Live audio service+1c; not in SER1.
  MusicHistoryState MusicHistory;  // Owner S6027c vector; persists between files.
  MusicExecutionPolicy MusicPolicy = MusicExecutionPolicy::Concrete;
  EffectContext Effects;
  RuleWipeState RuleWipe;
  QuakeOffsets Quake;
  std::optional<int32_t> ScreenFade;  // C1504, omitted from portable restored inputs.
  ScreenFilterState ScreenFilter;
  SceneLetterboxState Letterbox;
  CgHistoryState CgHistory;  // Owner state; survives file/context replacement.
  SceneImageState SceneImages;
  std::optional<int32_t> LastBackgroundId;  // Owner S+5e830, outside Bank0.
  std::optional<TitleSessionFields> TitleSession; // S5e814..5e82f; unknown in portable restores.
  // Requests to the host audio/effect/achievement services, not granted rewards
  // or evidence that a renderer played them. The host drains these each frame.
  std::vector<GameEvent> PendingEvents;
};

class UnsupportedCommand : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct LipActionCommand {
  bool Enabled=false;
  std::array<int16_t,5> Ids{};
  static LipActionCommand Decode(std::span<const uint8_t> payload);
};
void StartLipAction(SemanticState& state,const LipActionCommand& command);

class PayloadReader {
 public:
  explicit PayloadReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  uint8_t U8();
  uint16_t U16();
  uint32_t U32();
  int32_t I32();
  std::u16string String();
  Bytes Blob();
  size_t Remaining() const { return bytes_.size() - cursor_; }
  void CheckCount(uint32_t count, size_t minimumBytes) const;

 private:
  std::span<const uint8_t> bytes_;
  size_t cursor_ = 0;
};

struct ChoiceEntry {
  int32_t Destination;
  int16_t PreviewDescriptor = -1;
  std::u16string Label;
  std::vector<std::u16string> ExtraStrings;
};
struct ChoiceMenu {
  uint32_t DurationMs = 0;
  std::vector<ChoiceEntry> Entries;
};
struct ApproachEntry {
  std::u16string Label;
  std::u16string EnabledExpression;
  std::vector<ChoiceEntry> Children;
};

ChoiceMenu DecodeChoice(std::span<const uint8_t> payload, bool battle);
std::vector<ApproachEntry> DecodeApproach(std::span<const uint8_t> payload);
int32_t EvaluateNumeric(const SemanticState& state, const std::u16string& value);
// Comparison and homogeneous &&/|| chains cover all 51 expressions in the
// supplied corpus. Unverified mixed precedence/parenthesis syntax is rejected.
bool EvaluateCondition(const SemanticState& state, const std::u16string& value);

enum class UpdateRoute { Normal, Skipping };
struct CommandInput {
  UpdateRoute Route = UpdateRoute::Normal;
  std::optional<size_t> Choice;
  std::optional<size_t> Child;
  bool BattleTimeout = false;
  bool Advance = false;
  bool BattleResultAuxiliary = false;
  ProfileSelectionInput Profile;
  ApproachSelectionInput Approach;
  OrdinaryChoiceInput Ordinary;
};

struct MessageCommand {
  uint32_t Route;
  uint32_t Selector;
  int32_t VoiceId;
  int32_t SpeakerId;
  std::u16string Text;
  uint32_t CompletionPolicy;
  uint32_t Lane;
};
MessageCommand DecodeMessage(std::span<const uint8_t> payload);
struct StopSeCommand {
  int32_t Channel = 0, Fade = 0;
  static StopSeCommand Decode(std::span<const uint8_t> payload);
};
void StartStopSe(SemanticState& state, const StopSeCommand& command);

// Executes the supported command's semantic effects to its completion boundary.
// Presentation gates are supplied as already satisfied by a headless backend.
// Never consumes pending destinations; the dispatcher is a separate operation.
// Unknown commands fail explicitly. No recording targets or expected state enter
// this API. The interactive lifecycle/backend will build on these primitives.
void CompleteCommand(SemanticState& state, uint16_t opcode,
                     std::span<const uint8_t> payload, const CommandInput& input);

}  // namespace Impacto::Saekano
