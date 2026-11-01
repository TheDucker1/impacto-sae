#include "live2dstate.h"

#include "commands.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
void Live2dDriver::Restore(const Live2dState&, uint64_t) {
  throw UnsupportedCommand("model backend does not implement saved scene restore");
}
Live2dTransform Live2dDriver::CurrentTransform(const Live2dRecord&) const {
  throw UnsupportedCommand("model backend does not expose current transforms");
}
void Live2dDriver::SetTransform(const Live2dRecord&, const Live2dTransform&, uint64_t) {
  throw UnsupportedCommand("model backend does not implement mode transforms");
}
namespace {
float Float(PayloadReader& reader) {
  const float value = std::bit_cast<float>(reader.U32());
  if (!std::isfinite(value)) throw FormatError("non-finite Live2D parameter");
  return value;
}
void RequireKnown(const Live2dState& state) {
  if (!state.Known)
    throw UnsupportedCommand(
        "restored Live2D records/parameter slots are missing");
}
void RequireDriver(const Live2dCommand& command, const Live2dDriver& driver) {
  if (!driver.Supports(command.Selector))
    throw UnsupportedCommand("Live2D backend does not implement selector " +
                             std::to_string(command.Selector));
}
auto FindLogical(Live2dState& state, int32_t id) {
  return std::find_if(
      state.Records.begin(), state.Records.end(),
      [id](const auto& record) { return record.LogicalId == id; });
}
auto FindResource(Live2dState& state, int32_t id) {
  return std::find_if(
      state.Records.begin(), state.Records.end(),
      [id](const auto& record) { return record.ResourceId == id; });
}
Live2dRecord& EnsureLogical(Live2dState& state, int32_t id) {
  const auto found = FindLogical(state, id);
  if (found != state.Records.end()) return *found;
  auto& record = state.Records.emplace_back();
  record.LogicalId = id;
  return record;
}
void Apply(Live2dRecord& record, const Live2dCommand& command,
           Live2dDriver& driver, uint64_t now) {
  if (record.ResourceId)
    driver.Apply(record, command, now);
  else if (command.Selector != 6)
    throw UnsupportedCommand(
        "Live2D operation requires a loaded model resource");
}
}  // namespace

Live2dCommand DecodeLive2d(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  Live2dCommand command;
  command.Selector = reader.U32();
  switch (command.Selector) {
    case 2:
    case 3:
    case 4:
    case 6:
    case 12:
    case 13:
    case 15:
    case 16:
    case 17:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
      command.Target = reader.I32();
      break;
    default:
      // These selectors consume no further fields in the native loader. Their
      // execution remains unsupported until explicitly implemented below.
      return command;
  }
  switch (command.Selector) {
    case 2:
    case 4:
    case 6:
    case 16:
    case 17:
    case 20:
    case 23:
    case 24:
    case 25:
      command.Value = reader.I32();
      break;
    case 3:
      command.Value = reader.U32() != 0;
      break;
    case 12:
    case 13:
      command.Vector = {Float(reader), Float(reader)};
      command.Transition = reader.I32();
      break;
    case 21:
      command.Color[3] = uint8_t(reader.U32());
      command.Color[2] = uint8_t(reader.U32());
      command.Color[1] = uint8_t(reader.U32());
      command.Color[0] = uint8_t(reader.U32());
      break;
    case 22:
      command.Vector[0] = Float(reader);
      break;
  }
  // Record alignment padding is not extra command data and is not executed.
  return command;
}

void StartLive2d(Live2dState& state, const Live2dCommand& command,
                 Live2dDriver& driver, uint64_t now) {
  RequireKnown(state);
  if (command.Selector == 20)
    return;  // Stored by loader, no native Start effect.
  if (command.Selector == 22) {
    // No break: registration deliberately writes both eligible slots.
    for (auto& slot : state.Parameters)
      if (slot.Target == -1 || slot.Target == command.Target)
        slot = {command.Target, command.Vector[0]};
    return;
  }
  RequireDriver(command, driver);
  switch (command.Selector) {
    case 2:
    case 3:
    case 4:
    case 6:
    case 12:
    case 13:
    case 15:
    case 16:
    case 17:
    case 19:
    case 21:
    case 23:
    case 24:
    case 25:
      break;
    default:
      throw UnsupportedCommand(
          "Live2D selector lifecycle is not implemented: " +
          std::to_string(command.Selector));
  }
  if (command.Selector == 2) {
    if (!driver.ResourceReady(command.Target))
      driver.RequestResource(command.Target, command.Value, now);
    return;
  }
  if (command.Selector == 19)
    return;  // Common Update completes after the gate.
  if (command.Selector == 15) {
    const auto found = FindLogical(state, command.Target);
    if (found != state.Records.end()) {
      driver.Remove(*found);
      state.Records.erase(found);
    }
    return;
  }
  auto& record = EnsureLogical(state, command.Target);
  switch (command.Selector) {
    case 3:
      record.Enabled = command.Value != 0;
      break;
    case 6:
      record.Pose = std::bit_cast<int16_t>(uint16_t(command.Value));
      break;
    case 12:
      record.Position = command.Vector;
      break;
    case 13:
      record.Scale = command.Vector;
      break;
    case 16:
    case 17:
      record.AnimationIndex = command.Value;
      break;
    case 21:
      record.Color = command.Color;
      break;
    case 4:
    case 23:
    case 24:
    case 25:
      break;
    default:
      throw UnsupportedCommand(
          "Live2D selector lifecycle is not implemented: " +
          std::to_string(command.Selector));
  }
  auto applied = command;
  if (command.Selector == 6) applied.Value = *record.Pose;
  Apply(record, applied, driver, now);
}

bool UpdateLive2d(Live2dState& state, const Live2dCommand& command,
                  Live2dDriver& driver, bool skipping, uint64_t now) {
  RequireKnown(state);
  if (command.Selector != 20 && command.Selector != 22)
    RequireDriver(command, driver);
  if (!driver.GateReady()) return false;
  if (command.Selector == 2) {
    RequireDriver(command, driver);
    if (!driver.ResourceReady(command.Target)) {
      driver.RequestResource(command.Target, command.Value, now);
      return false;
    }
    auto found = FindResource(state, command.Target);
    if (found == state.Records.end()) {
      Live2dRecord record;
      record.ResourceId = command.Target;
      record.LogicalId = command.Value;
      state.Records.push_back(record);
      found = state.Records.end() - 1;
    }
    // Reloading an existing resource does not rename its logical record. The
    // incoming logical ID still selects the native configuration tables.
    driver.Attach(*found, command.Value, now);
  } else if (skipping && (command.Selector == 12 || command.Selector == 13)) {
    RequireDriver(command, driver);
    const auto found = FindLogical(state, command.Target);
    if (found == state.Records.end())
      throw UnsupportedCommand("Live2D skip requires the started model record");
    auto immediate = command;
    immediate.Transition = 0;
    Apply(*found, immediate, driver, now);
  }
  return true;
}

std::optional<float> Live2dParameterValue(const Live2dState& state,
                                          int32_t logicalId) {
  RequireKnown(state);
  std::optional<float> value;
  for (const auto& slot : state.Parameters) {
    if (slot.Target == -1) break;
    if (slot.Target == logicalId) value = slot.Value;
  }
  return value;
}

}  // namespace Impacto::Saekano
