#pragma once

#include "formats.h"

#include <array>
#include <map>
#include <optional>
#include <string>

namespace Impacto::Saekano {

struct CharacterState {
  std::vector<uint32_t> PoseHistory;
  uint16_t Priority = 0;
};

struct SemanticState {
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
  std::map<uint16_t, CharacterState> Characters;
  std::array<uint32_t, 2> WindowOpenness{};
  std::array<uint32_t, 2> WindowTypes{};
};

class UnsupportedCommand : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class PayloadReader {
 public:
  explicit PayloadReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
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

// Executes the supported command's semantic effects to its completion boundary.
// Presentation gates are supplied as already satisfied by a headless backend.
// Never consumes pending destinations; the dispatcher is a separate operation.
// Unknown commands fail explicitly. No recording targets or expected state enter
// this API. The interactive lifecycle/backend will build on these primitives.
void CompleteCommand(SemanticState& state, uint16_t opcode,
                     std::span<const uint8_t> payload, const CommandInput& input);

}  // namespace Impacto::Saekano
