#include "nativesave.h"

#include "commands.h"

#include <bit>
#include <cmath>
#include <set>

namespace Impacto::Saekano {
NativeContextSave DecodeNativeContextSave(std::span<const uint8_t> bytes) {
  NativeContextSave result;
  result.Images = DecodeNativeImageSavePrefix(bytes);
  result.Characters = DecodeNativeCharacterSavePrefix(bytes);
  size_t cursor = result.Images.EndOffset;
  auto read = [&](size_t width) {
    if (cursor > bytes.size() || width > bytes.size() - cursor)
      throw FormatError("truncated native saved context");
    uint32_t value = 0;
    for (size_t i = 0; i < width; ++i)
      value |= uint32_t(bytes[cursor++]) << (8 * i);
    return value;
  };
  auto field = [&](uint32_t offset, uint8_t width = 4) {
    auto value = read(width);
    result.Fields.push_back({offset, value, width});
    return value;
  };
  auto color = [&](uint32_t offset) {
    const auto packed = read(4);
    constexpr std::array<unsigned, 4> Shift{16, 8, 0, 24};
    for (size_t i = 0; i < 4; ++i)
      result.Fields.push_back({offset + uint32_t(i),
                               uint8_t(packed >> Shift[i]), 1});
  };
  auto count = [&](size_t rowSize) {
    const auto value = read(4);
    if (value > (bytes.size() - cursor) / rowSize)
      throw FormatError("invalid native saved context count");
    return value;
  };
  result.SavedCommand = std::bit_cast<int32_t>(read(4));
  result.File = std::bit_cast<int32_t>(read(4));
  result.SavedNextCommand = std::bit_cast<int32_t>(read(4));
  result.ResumeCommand = result.SavedNextCommand == -1
                            ? result.SavedCommand : result.SavedNextCommand;
  field(0x1c54, 1);
  // Native769dc ignores the serialized count and always reads five shorts.
  // Reject inconsistent metadata rather than reproduce its unchecked behavior.
  if (read(4) != result.Shorts.size())
    throw FormatError("native saved short array must contain five entries");
  for (auto& value : result.Shorts) value = uint16_t(read(2));
  for (uint32_t offset : {0x15ac, 0x15b0, 0x15b4, 0x15b8, 0x15bc})
    field(offset);
  // Existing character decoder includes the following default-color word2038.
  cursor = result.Characters.EndOffset;
  for (uint32_t offset : {0x20b4, 0x20c0, 0x20b8, 0x20c8,
                          0x20cc, 0x20c4, 0x20bc}) field(offset);
  const auto codeUnits = count(2);
  for (uint32_t i = 0; i < codeUnits; ++i)
    result.StringCodeUnits.push_back(uint16_t(read(2)));
  field(0x1b9e, 2);
  const auto listCount = count(12);
  for (uint32_t i = 0; i < listCount; ++i)
    result.List.push_back({read(4), read(4), read(4)});
  field(0x20d8);
  color(0x1c6c);
  field(0x1c64);
  field(0x1c68);
  field(0x1c60, 1);
  field(0x14518);
  // Native9e144 uses the fixed destination span, independent of this count.
  if (read(4) != result.FixedRecords.size())
    throw FormatError("native saved fixed array must contain six entries");
  for (auto& row : result.FixedRecords)
    for (auto& value : row) value = read(4);
  field(0x150c);
  field(0x1bf1, 1);
  for (uint32_t offset : {0x1c00, 0x1bfc, 0x1c04, 0x1c10}) field(offset);
  color(0x1c18);
  const auto tableOffset = cursor;
  const auto models = count(43);
  cursor += size_t(models) * 43;
  read(4);
  read(4);
  result.Models = DecodeNativeLive2dSaveTail(bytes, cursor, models);
  if (result.Models.TableOffset != tableOffset)
    throw FormatError("native saved model table offset mismatch");
  result.EndOffset = cursor;
  return result;
}
NativeLive2dSaveTail DecodeNativeLive2dSaveTail(std::span<const uint8_t> bytes,
                                                size_t used, uint32_t count) {
  constexpr size_t RecordSize = 43;
  constexpr size_t CountAndFinalWords = 12;
  if (used > bytes.size() || used < CountAndFinalWords ||
      count > (used - CountAndFinalWords) / RecordSize)
    throw FormatError("truncated native Live2D save tail");
  NativeLive2dSaveTail result;
  result.TableOffset = used - CountAndFinalWords - size_t(count) * RecordSize;
  PayloadReader header(bytes.subspan(result.TableOffset, 4));
  if (header.U32() != count)
    throw FormatError(
        "native Live2D save count disagrees with captured vector");
  result.Records.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto row = bytes.subspan(
        result.TableOffset + 4 + size_t(i) * RecordSize, RecordSize);
    PayloadReader fields(row.first(24));
    NativeSavedLive2dRecord record;
    record.LogicalId = fields.I32();
    record.ResourceId = fields.I32();
    record.PositionBits = {fields.U32(), fields.U32()};
    record.ScaleBits = {fields.U32(), fields.U32()};
    record.EnabledByte = row[24];
    record.Color = {row[27], row[26], row[25], row[28]};
    PayloadReader tail(row.subspan(29));
    record.Opaque20 = tail.U32();
    record.AnimationIndex = tail.I32();
    record.Opaque28 = tail.U32();
    record.Pose = std::bit_cast<int16_t>(tail.U16());
    result.Records.push_back(record);
  }
  PayloadReader final(bytes.subspan(used - 8, 8));
  result.FinalWords = {final.U32(), final.U32()};
  return result;
}
Live2dState RestoreNativeLive2dRecords(
    const NativeLive2dSaveTail& saved,
    const std::array<Live2dParameterSlot, 2>& parameters) {
  Live2dState result;
  std::set<int32_t> resources;
  result.Parameters = parameters;
  for (const auto& parameter : parameters)
    if (!std::isfinite(parameter.Value))
      throw FormatError("non-finite restored Live2D parameter");
  const auto floats = [](const std::array<uint32_t, 2>& bits) {
    std::array<float, 2> values{std::bit_cast<float>(bits[0]),
                                std::bit_cast<float>(bits[1])};
    for (float value : values)
      if (!std::isfinite(value))
        throw FormatError("non-finite saved Live2D transform");
    return values;
  };
  for (const auto& savedRecord : saved.Records) {
    if (savedRecord.ResourceId < 0 || savedRecord.LogicalId < 0 ||
        !resources.insert(savedRecord.ResourceId).second)
      throw FormatError("invalid or duplicate saved Live2D resource");
    Live2dRecord record;
    record.LogicalId = savedRecord.LogicalId;
    record.ResourceId = savedRecord.ResourceId;
    record.Position = floats(savedRecord.PositionBits);
    record.Scale = floats(savedRecord.ScaleBits);
    record.Enabled = savedRecord.EnabledByte != 0;
    record.Color = savedRecord.Color;
    record.AnimationIndex = savedRecord.AnimationIndex;
    record.Pose = savedRecord.Pose;
    result.Records.push_back(record);
  }
  return result;
}
Live2dState DecodeNativeLive2dRestorePacket(std::span<const uint8_t> packet) {
  PayloadReader reader(packet);
  if (reader.U32() != 0x3152324c)
    throw FormatError("invalid L2R1 bridge version");
  const auto mode = reader.U32(), ready = reader.U32();
  if (mode > 1) throw FormatError("invalid Live2D restore input mode");
  if (ready != 1)
    throw UnsupportedCommand("pending native resource jobs cannot be restored");
  const auto count = reader.U32(), used = reader.U32();
  const auto blob = reader.Blob();
  NativeLive2dSaveTail saved;
  if (mode == 0) {
    if (count || used || !blob.empty())
      throw FormatError("nonempty direct-empty model input");
  } else
    saved = DecodeNativeLive2dSaveTail(blob, used, count);
  std::array<Live2dParameterSlot, 2> parameters;
  for (auto& parameter : parameters) {
    parameter.Target = reader.I32();
    parameter.Value = std::bit_cast<float>(reader.U32());
  }
  if (reader.Remaining()) throw FormatError("trailing Live2D restore input");
  return RestoreNativeLive2dRecords(saved, parameters);
}
}  // namespace Impacto::Saekano
