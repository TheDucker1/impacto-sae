#include "nativesave.h"

#include "commands.h"

#include <bit>

namespace Impacto::Saekano {
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
}  // namespace Impacto::Saekano
