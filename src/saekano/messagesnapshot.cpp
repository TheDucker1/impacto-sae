#include "messagesnapshot.h"
#include "nativesave.h"

#include <bit>

namespace Impacto::Saekano {
namespace {
constexpr std::array<uint32_t, 4> Offsets{0x20b8, 0x20c0, 0x20c8, 0x20cc};
std::array<size_t, 4> Fields(const NativeContextSave& saved) {
  std::array<size_t, 4> result{};
  for (size_t i = 0; i < Offsets.size(); ++i) {
    std::optional<size_t> found;
    for (size_t j = 0; j < saved.Fields.size(); ++j) {
      const auto& field = saved.Fields[j];
      if (field.ManagerOffset != Offsets[i]) continue;
      if (field.Width != 4 || found)
        throw FormatError("invalid message snapshot cursor field");
      found = j;
    }
    if (!found) throw FormatError("missing message snapshot cursor field");
    result[i] = *found;
  }
  return result;
}
}  // namespace
MessageSnapshotCursors FreshMessageSnapshotCursors() {
  return {{-1, -1}, {-1, -1}};
}
MessageSnapshotCursors RestoreMessageSnapshotCursors(const NativeContextSave& saved) {
  const auto fields = Fields(saved);
  MessageSnapshotCursors result;
  for (size_t i = 0; i < 2; ++i) {
    result.SpeakerIds[i] = std::bit_cast<int32_t>(saved.Fields[fields[i]].Value);
    result.Commands[i] = std::bit_cast<int32_t>(saved.Fields[fields[i + 2]].Value);
  }
  return result;
}
void ProjectMessageSnapshotCursors(NativeContextSave& saved,
                                   const MessageSnapshotCursors& cursors) {
  const auto fields = Fields(saved);
  for (size_t i = 0; i < 2; ++i)
    if (!cursors.SpeakerIds[i] || !cursors.Commands[i])
      throw FormatError("unknown message snapshot cursors cannot be serialized");
  for (size_t i = 0; i < 2; ++i) {
    saved.Fields[fields[i]].Value = uint32_t(*cursors.SpeakerIds[i]);
    saved.Fields[fields[i + 2]].Value = uint32_t(*cursors.Commands[i]);
  }
}
void ObserveMessageSnapshotCursors(MessageSnapshotCursors& cursors, bool split,
                                   uint16_t opcode, bool completed,
                                   int32_t speaker, int32_t command) {
  if (completed || opcode != 0x64 || !split) return;
  for (size_t i = 0; i < 2; ++i)
    if (cursors.SpeakerIds[i] && *cursors.SpeakerIds[i] == speaker)
      cursors.Commands[i] = command;
}
}  // namespace Impacto::Saekano
