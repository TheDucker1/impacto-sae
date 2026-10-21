#pragma once

#include "formats.h"

#include <array>

namespace Impacto::Saekano {
// The game serializes 43 bytes per model, not its 52-byte in-memory record.
// These are saved targets/selection fields. They do not contain SDK queues,
// current interpolated transforms, blink state or a restorable resource
// pointer.
struct NativeSavedLive2dRecord {
  int32_t LogicalId = 0, ResourceId = 0;
  std::array<uint32_t, 2> PositionBits{}, ScaleBits{};
  uint8_t EnabledByte = 0;
  std::array<uint8_t, 4> Color{};  // RGBA, decoded from native packed BGRA.
  uint32_t Opaque20 = 0, Opaque28 = 0;  // Preserve without dereferencing.
  int32_t AnimationIndex = 0;
  int16_t Pose = 0;
};
struct NativeLive2dSaveTail {
  size_t TableOffset = 0;
  std::vector<NativeSavedLive2dRecord> Records;
  std::array<uint32_t, 2> FinalWords{};  // Native M+1cfc, M+1d00.
};

// Decode only the proven serializer tail (80195ac0). 'used' comes from the
// native stream metadata and 'count' from an independently captured vector.
// Does not scan for plausible records, validate the opaque prefix or establish
// that this saved table equals a later live table. Callers must prove
// provenance and the native load contract before using it to initialize a VM.
NativeLive2dSaveTail DecodeNativeLive2dSaveTail(std::span<const uint8_t> bytes,
                                                size_t used, uint32_t count);
}  // namespace Impacto::Saekano
