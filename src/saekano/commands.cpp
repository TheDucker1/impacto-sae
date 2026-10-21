#include "commands.h"

#include <bit>
#include <charconv>
#include <limits>

namespace Impacto::Saekano {

uint16_t PayloadReader::U16() {
  if (Remaining() < 2) throw FormatError("truncated payload halfword");
  const uint16_t result = uint16_t(bytes_[cursor_]) |
                          (uint16_t(bytes_[cursor_ + 1]) << 8);
  cursor_ += 2;
  return result;
}
uint32_t PayloadReader::U32() {
  const uint32_t low = U16();
  return low | (uint32_t(U16()) << 16);
}
int32_t PayloadReader::I32() { return std::bit_cast<int32_t>(U32()); }
void PayloadReader::CheckCount(uint32_t count, size_t minimumBytes) const {
  if (count > Remaining() / minimumBytes)
    throw FormatError("payload element count exceeds remaining bytes");
}
std::u16string PayloadReader::String() {
  const auto count = U32();
  CheckCount(count, 2);
  std::u16string text;
  text.reserve(count);
  for (uint32_t i = 0; i < count; ++i) text.push_back(char16_t(U16()));
  return text;
}

ChoiceMenu DecodeChoice(std::span<const uint8_t> payload, bool battle) {
  PayloadReader reader(payload);
  ChoiceMenu menu;
  if (battle) menu.DurationMs = reader.U32();
  const auto count = reader.U32();
  reader.CheckCount(count, battle ? 14 : 12);
  menu.Entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    ChoiceEntry entry;
    entry.Destination = reader.I32();
    if (battle) entry.PreviewDescriptor = std::bit_cast<int16_t>(reader.U16());
    entry.Label = reader.String();
    const auto extras = reader.U32();
    reader.CheckCount(extras, 4);
    for (uint32_t j = 0; j < extras; ++j)
      entry.ExtraStrings.push_back(reader.String());
    menu.Entries.push_back(std::move(entry));
  }
  return menu;
}

std::vector<ApproachEntry> DecodeApproach(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  const auto count = reader.U32();
  reader.CheckCount(count, 12);
  std::vector<ApproachEntry> menu;
  menu.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    ApproachEntry entry;
    entry.Label = reader.String();
    entry.EnabledExpression = reader.String();
    const auto children = reader.U32();
    reader.CheckCount(children, 8);
    for (uint32_t j = 0; j < children; ++j) {
      ChoiceEntry child;
      child.Destination = reader.I32();
      child.Label = reader.String();
      entry.Children.push_back(std::move(child));
    }
    menu.push_back(std::move(entry));
  }
  return menu;
}

namespace {
int32_t Decimal(const std::u16string& value, size_t start) {
  std::string ascii;
  for (size_t i = start; i < value.size(); ++i) {
    if (value[i] > 127) throw FormatError("non-ASCII numeric expression");
    ascii.push_back(char(value[i]));
  }
  int32_t result = 0;
  const auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != ascii.data() + ascii.size())
    throw FormatError("invalid numeric expression");
  return result;
}

int16_t& Variable(SemanticState& state, uint32_t bank, uint32_t index) {
  return bank == 0 ? state.Bank0.at(index) : state.Bank1.at(index);
}

void FinishSelection(SemanticState& state, UpdateRoute route) {
  // Skipping wrappers set phase 2; common completion changes nonzero to 1.
  if (route == UpdateRoute::Skipping || state.SkipPhase != 0) state.SkipPhase = 1;
  // Native also consults uncaptured S+60231 to decide whether skip policy
  // continues. The portable fixtures use continue-skip; the bridge enforces it.
}
}  // namespace

int32_t EvaluateNumeric(const SemanticState& state, const std::u16string& value) {
  if (value.size() >= 2 && value[0] == u'?') {
    const auto index = Decimal(value, 2);
    if (index < 0) throw FormatError("negative variable index");
    return value[1] == u'L' ? state.Bank0.at(size_t(index)) : state.Bank1.at(size_t(index));
  }
  return Decimal(value, 0);
}

void CompleteCommand(SemanticState& state, uint16_t opcode,
                     std::span<const uint8_t> payload, const CommandInput& input) {
  PayloadReader reader(payload);
  switch (opcode) {
    case 0x0000: {
      const auto bank = reader.U32();
      const auto index = reader.U32();
      const auto operation = reader.U32();
      const auto operandKind = reader.U32();
      const auto operandValue = reader.U32();
      auto& destination = Variable(state, bank, index);
      const uint32_t operand = operandKind <= 1
          ? uint32_t(int32_t(Variable(state, operandKind, operandValue))) : operandValue;
      // Use unsigned arithmetic to make ARM low-16 truncation defined in C++.
      const uint32_t result = operation == 0 ? operand : operation == 1
          ? uint32_t(int32_t(destination)) + operand
          : uint32_t(int32_t(destination)) - operand;
      destination = std::bit_cast<int16_t>(uint16_t(result));
      return;
    }
    case 0x0065: {
      const auto menu = DecodeChoice(payload, false);
      if (!input.Choice) throw std::runtime_error("choice input required");
      const auto destination = menu.Entries.at(*input.Choice).Destination;
      state.PendingCommandIndex = destination;
      FinishSelection(state, input.Route);
      return;
    }
    case 0x0066:
      // Only the primary dialogue context is cleared by native Start.
      state.SpeakerId = -1;
      state.ActiveText.clear();
      state.Messages[0].clear();
      return;
    case 0x0202: {
      // Intentional correction: the compiler serialized a numeric ID. Native
      // instead reads it as a string length and may overread the next record.
      const auto characterId = reader.U32();
      if (characterId > std::numeric_limits<uint16_t>::max())
        throw FormatError("character ID exceeds runtime key width");
      state.Characters.at(uint16_t(characterId)).PoseHistory.clear();
      return;
    }
    case 0x02be:
      state.PendingCommandIndex = reader.I32();
      return;
    case 0x02bf:
      state.PendingFileId = reader.I32();
      return;
    case 0x03f2: {
      const auto menu = DecodeChoice(payload, true);
      // The native UI starts on entry 0; a timeout keeps its selected entry.
      // This API accepts the current ordinal so interactive timeout can retain it.
      if (!input.Choice) throw std::runtime_error("battle selection input required");
      state.PendingCommandIndex = menu.Entries.at(*input.Choice).Destination;
      return;
    }
    case 0x03f6: {
      const auto menu = DecodeApproach(payload);
      if (!input.Choice || !input.Child)
        throw std::runtime_error("approach parent and child input required");
      const auto& parent = menu.at(*input.Choice);
      if (EvaluateNumeric(state, parent.EnabledExpression) != 1)
        throw std::runtime_error("disabled approach parent");
      const auto& child = parent.Children.at(*input.Child);
      if (*input.Child >= 3) throw FormatError("approach child exceeds flag layout");
      auto& flag = state.ApproachFlags.at(*input.Choice * 3 + *input.Child);
      if (flag) throw std::runtime_error("approach child already used");
      state.PendingCommandIndex = child.Destination;
      flag = 1;
      FinishSelection(state, input.Route);
      return;
    }
    case 0x03f8: {
      const auto requiredGauge = reader.I32();
      std::array<int32_t, 2> destinations;
      for (auto& destination : destinations) {
        destination = reader.I32();
        reader.String();  // Result text; presentation backend consumes later.
        reader.I32();     // Mission resource ID.
      }
      state.PendingCommandIndex = destinations[state.ApproachGauge >= requiredGauge ? 0 : 1];
      FinishSelection(state, input.Route);
      return;
    }
    case 0x03fa: {
      const std::array<int32_t, 3> destinations{reader.I32(), reader.I32(), reader.I32()};
      const size_t result = state.BattleGauge < 50 ? 1 : state.BattleGauge > 50 ? 0 : 2;
      state.PendingCommandIndex = destinations[result];
      return;
    }
    case 0x03fc:
      state.ApproachFlags.fill(0);
      return;
    default:
      throw UnsupportedCommand("unsupported Saekano opcode " + std::to_string(opcode));
  }
}

}  // namespace Impacto::Saekano
