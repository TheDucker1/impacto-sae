#include "workingcontext.h"
#include "nativesave.h"
#include "slotdata.h"
#include <algorithm>

namespace Impacto::Saekano {
NativeWorkingContext MakeNativeWorkingContext(const NativeSlotRecord& slot) {
  const auto context = DecodeNativeContextSave(slot.Bytesad8);
  const std::array<uint32_t, 3> header{uint32_t(context.File),
      uint32_t(context.SavedCommand), uint32_t(context.SavedNextCommand)};
  if (slot.Words1ad8 != header)
    throw FormatError("working context header disagrees with slot bytes");
  return {slot.Bytesad8, header, {slot.Words1b94[2], slot.Words1b94[3]},
           slot.Byte1ba4, context.EndOffset, 0};
}
void RefreshNativeWorkingContext(NativeWorkingContext& working,
                                  const NativeContextSave& current, uint8_t mode) {
  // Encode completely before publication. Native stream capacity is4096; bytes
  // beyond the new used extent are retained, not rewritten as slot padding.
  const auto bytes = EncodeNativeContextSave(current);
  std::copy(bytes.begin(), bytes.end(), working.Buffer.begin());
  working.Header = {uint32_t(current.File), uint32_t(current.SavedCommand),
                     uint32_t(current.SavedNextCommand)};
  working.ResumeWords = {0, 0};
  working.Mode = mode;
  working.Used = bytes.size();
  ++working.Refreshes;
}
}  // namespace Impacto::Saekano
