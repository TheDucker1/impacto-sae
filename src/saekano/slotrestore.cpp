#include "slotrestore.h"
#include "commands.h"
#include "vm.h"
#include <bit>
#include <cmath>
#include <algorithm>

namespace Impacto::Saekano {
namespace {
std::unique_ptr<NativePendingSlotLoad> StageRecord(const NativeSlotRecord& source,size_t ordinal) {
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
}
std::unique_ptr<NativePendingSlotLoad> StageNativeSlotLoad(const NativeSlotData& slots,size_t ordinal) {
  if(slots.Records.size()!=NativeSlotData::SlotCount || ordinal>=slots.Records.size())
    throw FormatError("invalid native load slot selection");
  if(!slots.Records[ordinal].Bytes2b6[2])throw FormatError("cannot load an empty native slot");
  return StageRecord(slots.Records[ordinal],ordinal);
}
std::unique_ptr<NativePendingSlotLoad> StageNativeBacklogLoad(BacklogHistory& history,size_t ordinal) {
  auto pending=StageRecord(history.At(ordinal).Record,ordinal);
  pending->FromBacklog=true;history.Truncate(ordinal);return pending;
}
void ApplyNativeSlotWorkingOwner(SemanticState& owner,
                                 const NativePendingSlotLoad& pending) {
  const auto& saved = pending.Record;
  if ((!pending.FromBacklog && !saved.Bytes2b6[2]) || saved.Bytes2b6[1])
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
  next->WorkingDayTime = SavedDayTime{saved.Byte1b90, next->TimeComponents};
  owner = std::move(*next);
}
void ApplyNativeSlotContextValues(SemanticState& state,
                                  MessageRuntimeContext& message,
                                  const NativePendingSlotLoad& pending) {
  if (!state.MouthControl || !message.Known)
    throw UnsupportedCommand("saved context needs fresh message/mouth defaults");
  const auto& saved = pending.Context;
  const auto field = [&](uint32_t offset, uint8_t width = 4) {
    const auto found = std::find_if(saved.Fields.begin(), saved.Fields.end(),
        [&](const auto& f) { return f.ManagerOffset == offset && f.Width == width; });
    if (found == saved.Fields.end()) throw FormatError("missing native saved context field");
    return found->Value;
  };
  const auto word = [&](uint32_t offset) { return std::bit_cast<int32_t>(field(offset)); };
  auto next = std::make_unique<SemanticState>(state);
  auto context = message;
  context.SnapshotCursors = RestoreMessageSnapshotCursors(saved);
  next->SnapshotMetadata.RetainedString = saved.StringCodeUnits;
  next->SnapshotMetadata.RetainedShort = uint16_t(field(0x1b9e, 2));
  next->SnapshotMetadata.ScriptParticles = saved.FixedRecords;
  next->ScriptFileId = saved.File;
  next->CurrentCommandIndex = saved.ResumeCommand;
  next->PendingCommandIndex = -1;
  next->BgmTrack = word(0x15ac); next->BgmCommandVolume = word(0x15b0);
  next->SeTrack = word(0x15b4); next->SeCommandVolume = word(0x15b8);
  next->SeChannel = word(0x15bc);
  next->MouthControl->OverrideEnabled = field(0x1c54, 1) != 0;
  for (size_t i = 0; i < saved.Shorts.size(); ++i)
    next->MouthControl->OverrideIds[i] = std::bit_cast<int16_t>(saved.Shorts[i]);
  context.SplitDialogue = field(0x20b4) != 0;
  context.Side0Character = word(0x20bc);
  // C20ac is serialized split dialogue. C14a54 battle entry is an independent
  // constructor/payload-load field and is not inferred from this saved word.
  next->Battle.SideCharacters = {context.Side0Character, word(0x20c4)};
  // Only the first window's openness is serialized. The second window and
  // window-type selections retain their fresh/load-callback values.
  next->WindowOpenness[0] = field(0x20d8);
  next->DivergenceStack.clear();
  for (const auto& row : saved.List)
    next->DivergenceStack.push_back({std::bit_cast<int32_t>(row[0]),
        std::bit_cast<int32_t>(row[1]), std::bit_cast<int32_t>(row[2])});
  next->ScreenFade = word(0x150c);
  next->CharacterRuntimeState.MaximumPriority = field(0x14518);
  next->ScreenFilter.Mode = std::bit_cast<int32_t>(saved.Models.FinalWords[0]);
  next->ScreenFilter.Intensity = std::bit_cast<float>(saved.Models.FinalWords[1]);
  if (!std::isfinite(*next->ScreenFilter.Intensity))
    throw FormatError("non-finite native saved screen filter");
  next->Letterbox = {true, field(0x1c60, 1) != 0,
                     word(0x1c64), word(0x1c68), {}};
  for (size_t i = 0; i < 4; ++i)
    next->Letterbox.Color[i] = uint8_t(field(0x1c6c + uint32_t(i), 1));
  state = std::move(*next);
  message = context;
}
void BeginNativeRuleWipeRestore(SemanticState& state,
                                const NativeContextSave& saved,
                                SceneImageDriver& images,
                                NativeRuleWipeRestore& restore) {
  if (restore.Started) throw FormatError("native rule restore already started");
  if (!state.RuleWipe.Known || state.RuleWipe.Active || state.RuleWipe.Auxiliary ||
      state.RuleWipe.BackgroundResource)
    throw UnsupportedCommand("native rule restore needs a fresh rule owner");
  const auto field = [&](uint32_t offset, uint8_t width = 4) {
    const auto found = std::find_if(saved.Fields.begin(), saved.Fields.end(),
        [&](const auto& f) { return f.ManagerOffset == offset && f.Width == width; });
    if (found == saved.Fields.end()) throw FormatError("missing native saved rule field");
    return found->Value;
  };
  auto next = state.RuleWipe;
  next.Active = field(0x1bf1, 1) != 0;
  next.Parameter = std::bit_cast<int32_t>(field(0x1bfc));
  next.BackgroundKind = std::bit_cast<int32_t>(field(0x1c00));
  next.CachedRule = std::bit_cast<int32_t>(field(0x1c04));
  next.CachedBackground = std::bit_cast<int32_t>(field(0x1c10));
  for (size_t i = 0; i < next.Color.size(); ++i)
    next.Color[i] = uint8_t(field(0x1c18 + uint32_t(i), 1));
  if (!next.Active) {
    images.Release(ImageSlot::Rule);
    images.Release(ImageSlot::Background);
    next.CachedRule = next.CachedBackground = -1;
    next.Auxiliary = false;
    next.BackgroundResource.reset();
  } else {
    // Validate the complete request set before any resource side effect.
    if (next.CachedRule < 0 ||
        size_t(next.CachedRule) >= images.AssetCount(ImageSection::Rule))
      throw FormatError("saved active rule ID is outside the archive");
    if (next.BackgroundKind < 0 || next.BackgroundKind > 4)
      throw FormatError("saved rule background kind is invalid");
    if (next.BackgroundKind == 1 && next.CachedBackground >= 0)
      throw UnsupportedCommand(
          "saved retained rule background has no serialized resource category");
    if (next.BackgroundKind >= 2 && next.CachedBackground >= 0) {
      const auto section = next.BackgroundKind == 2 ? ImageSection::Background
          : next.BackgroundKind == 3 ? ImageSection::Event : ImageSection::Free;
      if (size_t(next.CachedBackground) >= images.AssetCount(section))
        throw FormatError("saved rule background ID is outside the archive");
      next.BackgroundResource = ImageKey{section, uint32_t(next.CachedBackground)};
    }
    next.Progress = 100;
    try {
      images.Prepare(ImageSlot::Rule, {ImageSection::Rule, uint32_t(next.CachedRule)});
      if (next.BackgroundResource)
        images.Prepare(ImageSlot::Background, *next.BackgroundResource);
    } catch (...) {
      images.Release(ImageSlot::Rule);
      images.Release(ImageSlot::Background);
      throw;
    }
  }
  state.RuleWipe = std::move(next);
  restore.Started = true;
}
bool PollNativeRuleWipeRestore(const SceneImageDriver& images,
                               NativeRuleWipeRestore& restore) {
  if (!restore.Started) throw FormatError("native rule restore has not started");
  if (restore.Ready) return true;
  if (!images.Ready()) return false;
  restore.Ready = true;
  return true;
}
void QueueNativeSlotLoadStops(SemanticState& state) {
  state.PendingEvents.push_back({GameEventKind::StopBgmWithCurrentFade, 0});
  state.PendingEvents.push_back({GameEventKind::StopSe, 2});
  state.PendingEvents.push_back({GameEventKind::StopSe, 3});
}
bool StepNativeSlotDeferredStart(SemanticState& state,
                                 NativeSlotDeferredStart& restore,
                                 bool resourcesReady,
                                 BgmResources* music, SeResources* sounds) {
  if (!resourcesReady) return false;
  auto next = restore;
  if (next.PendingPhase) {
    next.Phase = *next.PendingPhase;
    next.PendingPhase.reset();
  }
  if (next.Phase == 1) {
    if (!state.BgmTrack || !state.SeTrack)
      throw UnsupportedCommand("saved audio track state is incomplete");
    std::vector<GameEvent> events;
    if (*state.BgmTrack >= 0) {
      if (!music || !state.BgmMasterVolume || !state.BgmCommandVolume)
        throw UnsupportedCommand("saved BGM replay needs original media and live volume");
      auto request = BgmRequest(*state.BgmTrack, *state.BgmMasterVolume,
                                *state.BgmCommandVolume, music->Coefficient());
      music->Prepare(*state.BgmTrack);
      events.push_back({GameEventKind::PlayBgm, request.Track, {},
                        request.CommandVolume, request});
    }
    if (*state.SeTrack >= 0) {
      if (!sounds || !state.SeMasterVolume || !state.SeCommandVolume || !state.SeChannel)
        throw UnsupportedCommand("saved SE replay needs original media and live volume");
      if (*state.SeChannel < 0 || *state.SeChannel > 3)
        throw FormatError("saved SE channel is outside native SE slots");
      SePlayRequest request;
      request.Track = *state.SeTrack; request.Channel = *state.SeChannel + 2;
      request.CommandVolume = *state.SeCommandVolume; request.Remember = true;
      request.Volume = SeVolume(*state.SeMasterVolume, request.CommandVolume,
                                 sounds->Coefficient());
      request.RefreshServiceVolume = false;
      sounds->Prepare(request.Track);
      events.push_back({GameEventKind::PlaySe, request.Track, {},
                        request.CommandVolume, {}, request});
    }
    // Direct service replay does not execute ScriptPlayBgm/Se, update played
    // histories, or overwrite remembered context values.
    state.PendingEvents.insert(state.PendingEvents.end(), events.begin(), events.end());
    next.Channel.Start(255, 0, 12);
    next.Phase = 2;
  }
  bool start = false;
  if (next.Phase == 2) {
    next.Channel.Tick();
    if (next.Channel.Elapsed == next.Channel.Frames) {
      next.PendingPhase = 0;
      next.CommandStartIssued = true;
      start = true;
    }
  }
  restore = std::move(next);
  return start;
}
void RestoreNativeSlotModels(SemanticState& state, const NativeContextSave& saved,
                             Live2dDriver& driver, uint64_t now) {
  // Constructor801a0c14..0c54; neither slot appears in801a3730's stream.
  auto models = RestoreNativeLive2dRecords(saved.Models, {});
  driver.Restore(models, now);
  state.Live2d = std::move(models);
}
std::vector<NativeSavedParticleRequest> BuildNativeSlotParticleRequests(
    const NativeContextSave& saved, const SceneCameraParameters& camera,
    const ParticleEnvironment& environment, uint32_t opacityGroups) {
  if (opacityGroups > ParticleManager::GroupCount)
    throw FormatError("script particle opacity groups exceed the manager");
  std::vector<NativeSavedParticleRequest> result;
  for (size_t i = 0; i < saved.FixedRecords.size(); ++i) {
    const auto& row = saved.FixedRecords[i];
    const auto group = std::bit_cast<int32_t>(row[0]);
    if (group < 0) continue; // All remaining inactive-row bytes may be unwritten.
    const auto effect = std::bit_cast<int32_t>(row[1]);
    if (uint32_t(group) >= opacityGroups || effect < 0 ||
        size_t(effect) >= environment.Effects.size())
      throw FormatError("saved particle group/effect is outside authored tables");
    const auto prewarm = environment.Effects[size_t(effect)].Parameter;
    if (prewarm < 0) throw FormatError("negative saved particle prewarm");
    const float scale = environment.Scalar *
        (float(std::bit_cast<int32_t>(row[5])) / 100.0f);
    auto request = BuildApproachParticleRequest(
        {std::bit_cast<int32_t>(row[2]), std::bit_cast<int32_t>(row[3])},
        camera, scale);
    request.Group = uint32_t(group);
    // The native owner uses B,G,R,A float slots; the wire word is AARRGGBB.
    for (size_t c = 0; c < 4; ++c)
      request.Owner[10 + c] = float(uint8_t(row[4] >> (8 * c))) / 255.0f;
    result.push_back({i, size_t(effect), request, prewarm});
  }
  return result;
}
std::vector<NativeSavedParticleRequest> RestoreNativeSlotParticles(
    ParticleManager& manager, const GameResources& resources,
    const NativeContextSave& saved, const SceneCameraParameters& camera,
    const ParticleEnvironment& environment, uint32_t opacityGroups) {
  const auto requests = BuildNativeSlotParticleRequests(saved, camera, environment, opacityGroups);
  for (size_t group = 0; group < ParticleManager::GroupCount; ++group)
    for (const auto& handle : manager.Handles(group))
      if (handle.EffectId || handle.State.Running)
        throw UnsupportedCommand("saved particle restore needs the reset script manager");
  std::map<size_t, ConfiguredParticleEffect> prepared;
  for (const auto& request : requests)
    if (!manager.HasEffect(request.Effect) && !prepared.contains(request.Effect))
      prepared.emplace(request.Effect,
          ConfiguredParticleEffect::Load(resources, environment, request.Effect));
  manager.LoadTextureCatalog(resources);
  for (auto& [effect, assets] : prepared)
    manager.RegisterEffect(effect, std::move(assets));
  for (const auto& request : requests)
    if (!manager.Request(request.Effect, request.Request, request.PrewarmMilliseconds))
      throw UnsupportedCommand("saved particle resource did not register");
  return requests;
}
}
