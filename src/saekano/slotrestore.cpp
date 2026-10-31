#include "slotrestore.h"
#include "commands.h"
#include <bit>

namespace Impacto::Saekano {
std::unique_ptr<NativePendingSlotLoad> StageNativeSlotLoad(
    const NativeSlotData& slots, size_t ordinal) {
  if (slots.Records.size() != NativeSlotData::SlotCount || ordinal >= slots.Records.size())
    throw FormatError("invalid native load slot selection");
  const auto& source = slots.Records[ordinal];
  if (!source.Bytes2b6[2]) throw FormatError("cannot load an empty native slot");
  auto pending = std::make_unique<NativePendingSlotLoad>();
  pending->Ordinal = ordinal;
  pending->Record = source;
  pending->Record.Bytes2b6[1] = 0;
  pending->Context = DecodeNativeContextSave(source.Bytesad8);
  const auto& context = pending->Context;
  if (context.File < 0 || context.ResumeCommand < 0 ||
      std::bit_cast<int32_t>(source.Words1ad8[0]) != context.File ||
      std::bit_cast<int32_t>(source.Words1ad8[1]) != context.SavedCommand ||
      std::bit_cast<int32_t>(source.Words1ad8[2]) != context.SavedNextCommand)
    throw FormatError("native slot header disagrees with saved context");
  return pending;
}
void ApplyNativeSlotWorkingOwner(SemanticState& owner,
                                 const NativePendingSlotLoad& pending) {
  const auto& saved = pending.Record;
  if (!saved.Bytes2b6[2] || saved.Bytes2b6[1])
    throw FormatError("native pending load record is not staged");
  auto next = std::make_unique<SemanticState>(owner);
  TitleSessionFields session;
  session.Flags = {saved.Bytes2b6[2], saved.Bytes2b6[1], saved.Bytes2b6[0]};
  for (size_t i = 0; i < session.Words.size(); ++i)
    session.Words[i] = std::bit_cast<int32_t>(saved.Words2b8[i]);
  next->TitleSession = session;
  next->LastBackgroundId = std::bit_cast<int32_t>(saved.Words2b8[6]);
  for (size_t i = 0; i < next->Bank0.size(); ++i)
    next->Bank0[i] = std::bit_cast<int16_t>(saved.Shorts2d4[i]);
  next->ProfileOwner.CachedOrdinal = std::bit_cast<int32_t>(saved.Word1ba8);
  for (size_t i = 0; i < saved.Words1bac.size(); ++i)
    next->ProfileOwner.Stats[i] = std::bit_cast<int32_t>(saved.Words1bac[i]);
  next->ProfileOwner.Known = true;
  const auto text = [](const auto& units) {
    return std::u16string(units.begin(), units.end());
  };
  next->ChapterId = std::bit_cast<int32_t>(saved.Word1b54);
  next->Titles = {text(saved.Strings[1]), text(saved.Strings[0])};
  next->SavedChapter = ChapterTitleCommand{
      next->ChapterId, {next->Titles[0], next->Titles[1]}};
  next->DayEnabled = saved.Byte1b90;
  next->TimeComponents = {std::bit_cast<int32_t>(saved.Words1b94[0]),
                           std::bit_cast<int32_t>(saved.Words1b94[1])};
  owner = std::move(*next);
}
}
