#pragma once

#include "formats.h"
#include "live2dstate.h"
#include "imagesave.h"
#include "character.h"

#include <array>

namespace Impacto::Saekano {
// Offsets name verified native ScriptManager destinations; fields whose game
// meaning is not established stay opaque. These are values, never heap pointers.
struct NativeSavedContextField {
  uint32_t ManagerOffset = 0, Value = 0;
  uint8_t Width = 4;
};
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
struct NativeContextSave {
  NativeImageSavePrefix Images;
  NativeCharacterSavePrefix Characters;
  NativeLive2dSaveTail Models;
  int32_t SavedCommand = -1, File = -1, SavedNextCommand = -1;
  int32_t ResumeCommand = -1;
  std::vector<NativeSavedContextField> Fields;
  std::array<uint16_t, 5> Shorts{};  // M+1c56.
  std::vector<uint16_t> StringCodeUnits;  // M+1b64 dual string, UTF-16.
  std::vector<std::array<uint32_t, 3>> List;  // M+14f7c.
  // M+14488, six 24-byte records. Fifth word is packed AARRGGBB on disk.
  std::array<std::array<uint32_t, 6>, 6> FixedRecords{};
  size_t EndOffset = 0;
};
// Walk the full native801a3730 stream. The containing slot reserves4096 bytes;
// unused padding is not serialized state. No captured used/count is required.
// Resource reconstruction and binding to scene owners remain separate steps.
NativeContextSave DecodeNativeContextSave(std::span<const uint8_t> bytes);

// Decode only the proven serializer tail (80195ac0). 'used' comes from the
// native stream metadata and 'count' from an independently captured vector.
// Does not scan for plausible records, validate the opaque prefix or establish
// that this saved table equals a later live table. Callers must prove
// provenance and the native load contract before using it to initialize a VM.
NativeLive2dSaveTail DecodeNativeLive2dSaveTail(std::span<const uint8_t> bytes,
                                                size_t used, uint32_t count);
// Native801a3730 rebuilds the record vector;80072630 then starts fresh model
// animation state, snaps saved transforms, selects saved loop/expression.
// Persistent slots are separate captured context fields, not part of this tail.
Live2dState RestoreNativeLive2dRecords(
    const NativeLive2dSaveTail& saved,
    const std::array<Live2dParameterSlot, 2>& parameters);
// L2R1 is our explicit bridge packet, not a native game format. It carries
// matched save bytes/used/count and independently captured parameter slots.
Live2dState DecodeNativeLive2dRestorePacket(std::span<const uint8_t> packet);
}  // namespace Impacto::Saekano
