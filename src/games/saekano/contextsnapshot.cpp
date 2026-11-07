#include "contextsnapshot.h"
#include "commands.h"
#include "nativesave.h"
#include "vm.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
ContextSnapshotMetadata MakeFreshContextSnapshotMetadata() {
  ContextSnapshotMetadata metadata;
  metadata.RetainedString.emplace(); // Native VitaDualString_Construct: empty.
  metadata.RetainedShort=0; // Native leaves story ID unwritten until opcode02bd Load.
  metadata.ScriptParticles.emplace();
  for(auto& row:*metadata.ScriptParticles)row[0]=0xffffffff;
  // Constructor initializes only group=-1/color0. Value initialization also
  // clears four unused words per inactive row instead of saving heap residue.
  metadata.WorkingMode=0;
  return metadata;
}
NativeContextSave CaptureNativeContext(const SemanticState& state,
                                        const MessageRuntimeContext& message) {
  if (!message.Known || !state.MouthControl || !state.Battle.Known ||
      !state.SceneImages.Known || !state.RuleWipe.Known || !state.Letterbox.Known ||
      !std::all_of(state.SceneImages.FillRectsKnown.begin(),
                   state.SceneImages.FillRectsKnown.end(), [](bool v) { return v; }))
    throw UnsupportedCommand("native snapshot needs complete current context owners");
  const auto known = [](const auto& value, const char* reason) {
    if (!value) throw UnsupportedCommand(reason);
    return *value;
  };
  if (state.ScriptFileId < 0 || state.CurrentCommandIndex < 0)
    throw FormatError("native snapshot has invalid current script site");
  NativeContextSave saved;
  saved.Images = state.SceneImages.Slots;
  saved.Characters = CaptureNativeCharacters(state);
  saved.Models.Records = CaptureNativeLive2dRecords(state.Live2d);
  saved.File = state.ScriptFileId;
  saved.SavedCommand = state.CurrentCommandIndex;
  // Native1498 is the first async command, not the selected16a8 jump target.
  saved.SavedNextCommand = state.FirstAsyncCommandIndex;
  saved.ResumeCommand = saved.SavedNextCommand == -1
                            ? saved.SavedCommand : saved.SavedNextCommand;
  const auto field = [&](uint32_t offset, uint32_t value, uint8_t width = 4) {
    saved.Fields.push_back({offset, value, width});
  };
  const auto color = [&](uint32_t offset, const auto& rgba) {
    for (size_t i = 0; i < 4; ++i) field(offset + uint32_t(i), rgba[i], 1);
  };
  field(0x1c54, state.MouthControl->OverrideEnabled, 1);
  for (size_t i = 0; i < saved.Shorts.size(); ++i)
    saved.Shorts[i] = uint16_t(state.MouthControl->OverrideIds[i]);
  field(0x15ac, uint32_t(known(state.BgmTrack, "unknown remembered BGM track")));
  field(0x15b0, uint32_t(known(state.BgmCommandVolume, "unknown remembered BGM volume")));
  field(0x15b4, uint32_t(known(state.SeTrack, "unknown remembered SE track")));
  field(0x15b8, uint32_t(known(state.SeCommandVolume, "unknown remembered SE volume")));
  field(0x15bc, uint32_t(known(state.SeChannel, "unknown remembered SE channel")));
  field(0x20b4, message.SplitDialogue);
  for (uint32_t offset : {0x20c0, 0x20b8, 0x20c8, 0x20cc}) field(offset, 0);
  ProjectMessageSnapshotCursors(saved, message.SnapshotCursors);
  field(0x20c4, uint32_t(state.Battle.SideCharacters[1]));
  field(0x20bc, uint32_t(message.Side0Character));
  saved.StringCodeUnits = known(state.SnapshotMetadata.RetainedString,
                                 "unknown retained context string");
  field(0x1b9e, known(state.SnapshotMetadata.RetainedShort,
                      "unknown retained context short"), 2);
  for (const auto& row : state.DivergenceStack)
    saved.List.push_back({uint32_t(row[0]), uint32_t(row[1]), uint32_t(row[2])});
  field(0x20d8, state.WindowOpenness[0]);
  color(0x1c6c, state.Letterbox.Color);
  field(0x1c64, uint32_t(state.Letterbox.Top));
  field(0x1c68, uint32_t(state.Letterbox.Bottom));
  field(0x1c60, state.Letterbox.Foreground, 1);
  field(0x14518, known(state.CharacterRuntimeState.MaximumPriority,
                       "unknown character maximum priority"));
  saved.FixedRecords = known(state.SnapshotMetadata.ScriptParticles,
                              "unknown script particle snapshot records");
  field(0x150c, uint32_t(known(state.ScreenFade, "unknown screen fade")));
  field(0x1bf1, state.RuleWipe.Active, 1);
  field(0x1c00, uint32_t(state.RuleWipe.BackgroundKind));
  field(0x1bfc, uint32_t(state.RuleWipe.Parameter));
  field(0x1c04, uint32_t(state.RuleWipe.CachedRule));
  field(0x1c10, uint32_t(state.RuleWipe.CachedBackground));
  color(0x1c18, state.RuleWipe.Color);
  const auto mode = known(state.ScreenFilter.Mode, "unknown screen filter mode");
  const auto intensity = known(state.ScreenFilter.Intensity, "unknown screen filter intensity");
  if (!std::isfinite(intensity)) throw FormatError("non-finite saved screen filter");
  saved.Models.FinalWords = {uint32_t(mode), std::bit_cast<uint32_t>(intensity)};
  return saved;
}
}  // namespace Impacto::Saekano
