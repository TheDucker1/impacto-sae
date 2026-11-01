#pragma once
#include "formats.h"
#include <array>

namespace Impacto::Saekano {
struct NativeSlotRecord;
struct NativeContextSave;
// Only the verified context portion of S5e814's working record. This is not a
// complete current save slot: shared bank/chapter/profile ownership is separate.
struct NativeWorkingContext {
  std::array<uint8_t, 4096> Buffer{};
  std::array<uint32_t, 3> Header{};  // S60038/3c/40: file/current/first-async.
  std::array<uint32_t, 2> ResumeWords{};  // S600fc/60100.
  uint8_t Mode = 0;  // S60104.
  size_t Used = 0;
  uint64_t Refreshes = 0;  // Diagnostic, not serialized.
};
NativeWorkingContext MakeNativeWorkingContext(const NativeSlotRecord& slot);
void RefreshNativeWorkingContext(NativeWorkingContext& working,
                                  const NativeContextSave& current, uint8_t mode);
}  // namespace Impacto::Saekano
