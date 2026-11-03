#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Impacto::Saekano {
struct NativeContextSave;
// Manager20b8/20c0 are compared with current speaker15a4; matching sides receive
// current command1490 at20c8/20cc while opcode64 remains incomplete. Their other
// producers are not yet established. Omitted portable input stays unknown.
struct MessageSnapshotCursors {
  std::array<std::optional<int32_t>, 2> SpeakerIds{}, Commands{};
};
// Native reset writes all four fields to-1. Apply only on manager replacement,
// never to fill metadata omitted by a portable restore.
MessageSnapshotCursors FreshMessageSnapshotCursors();
MessageSnapshotCursors RestoreMessageSnapshotCursors(const NativeContextSave& saved);
void ProjectMessageSnapshotCursors(NativeContextSave& saved,
                                   const MessageSnapshotCursors& cursors);
void ObserveMessageSnapshotCursors(MessageSnapshotCursors& cursors, bool split,
                                   uint16_t opcode, bool completed,
                                   int32_t speaker, int32_t command);
}  // namespace Impacto::Saekano
