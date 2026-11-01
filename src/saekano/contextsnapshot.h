#pragma once
#include "formats.h"
#include <array>
#include <optional>

namespace Impacto::Saekano {
struct SemanticState;
struct MessageRuntimeContext;
struct NativeContextSave;
// Retained native value owners whose wider gameplay consumers are unresolved.
// These are copied by Load, not borrowed source bytes or a cached entire slot.
// Unknown portable/fresh fields cannot be filled by the snapshot exporter.
struct ContextSnapshotMetadata {
  std::optional<std::vector<uint16_t>> RetainedString;
  std::optional<uint16_t> RetainedShort;
  std::optional<std::array<std::array<uint32_t, 6>, 6>> ScriptParticles;
  std::optional<uint8_t> WorkingMode;  // Manager1b60, outside context stream.
};
// Current owner -> native typed stream. Resource reconstruction must have
// completed. Wire offsets are produced by the encoder/decoder, not this builder.
NativeContextSave CaptureNativeContext(const SemanticState& state,
                                        const MessageRuntimeContext& message);
}  // namespace Impacto::Saekano
