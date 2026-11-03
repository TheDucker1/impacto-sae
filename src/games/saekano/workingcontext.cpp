#include "workingcontext.h"
#include "nativesave.h"
#include "slotdata.h"
#include "commands.h"
#include <algorithm>
#include <bit>

namespace Impacto::Saekano {
NativeWorkingRecord MakeFreshNativeWorkingRecord() {
  NativeWorkingRecord working;
  working.Retained.Words1ad8[2] = 0xffffffff;
  working.Retained.Word1b54 = 0xffffffff;
  working.Retained.Words1b94[2] = working.Retained.Words1b94[3] = 0xffffffff;
  working.Retained.Words2b8[6] = 0xffffffff;
  working.Context.Header = working.Retained.Words1ad8;
  working.Context.ResumeWords = {0xffffffff, 0xffffffff};
  // Native leaves day/time/mode unwritten. Value initialization is intentional;
  // no blank surface or fabricated context encoding is substituted here.
  return working;
}
void InitializeFreshWorkingOwner(SemanticState& owner, const TitleSessionSeeds& seeds) {
  if (owner.WorkingSave || owner.SavedChapter || owner.WorkingDayTime || !owner.ProfileOwner.Known)
    throw FormatError("fresh working owner would overwrite existing or unknown state");
  auto working = MakeFreshNativeWorkingRecord();
  ResetTitleSession(owner, seeds);  // Validates authored seed extents before writes.
  owner.SavedChapter = ChapterTitleCommand{-1, {}};
  owner.WorkingDayTime = SavedDayTime{};
  owner.WorkingSave = std::move(working);
}
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
NativeSlotRecord ComposeNativeWorkingRecord(const NativeSlotRecord& retained,
    const SemanticState& current, const NativeWorkingContext& context) {
  if (!current.TitleSession || !current.LastBackgroundId ||
      !current.ProfileOwner.Known || !current.SavedChapter || !current.WorkingDayTime)
    throw UnsupportedCommand("working record needs known session/profile/chapter/day owners");
  NativeSlotRecord result = retained;
  const auto& flags = current.TitleSession->Flags;
  result.Bytes2b6 = {flags[2], flags[1], flags[0]};
  for (size_t i = 0; i < current.TitleSession->Words.size(); ++i)
    result.Words2b8[i] = std::bit_cast<uint32_t>(current.TitleSession->Words[i]);
  result.Words2b8[6] = std::bit_cast<uint32_t>(*current.LastBackgroundId);
  for (size_t i = 0; i < current.Bank0.size(); ++i)
    result.Shorts2d4[i] = std::bit_cast<uint16_t>(current.Bank0[i]);
  result.Word1ba8 = std::bit_cast<uint32_t>(current.ProfileOwner.CachedOrdinal);
  for (size_t i = 0; i < current.ProfileOwner.Stats.size(); ++i)
    result.Words1bac[i] = std::bit_cast<uint32_t>(current.ProfileOwner.Stats[i]);
  const auto& chapter = *current.SavedChapter;
  result.Word1b54 = std::bit_cast<uint32_t>(chapter.Id);
  result.Strings[0].assign(chapter.Titles[1].begin(), chapter.Titles[1].end());
  result.Strings[1].assign(chapter.Titles[0].begin(), chapter.Titles[0].end());
  result.Byte1b90 = current.WorkingDayTime->Enabled;
  for (size_t i = 0; i < 2; ++i)
    result.Words1b94[i] = std::bit_cast<uint32_t>(current.WorkingDayTime->Components[i]);
  result.Bytesad8 = context.Buffer;
  result.Words1ad8 = context.Header;
  std::copy(context.ResumeWords.begin(), context.ResumeWords.end(), result.Words1b94.begin() + 2);
  result.Byte1ba4 = context.Mode;
  return result;
}
}  // namespace Impacto::Saekano
