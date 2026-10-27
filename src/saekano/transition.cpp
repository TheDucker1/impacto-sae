#include "transition.h"

#include "commands.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
int32_t Signed(uint32_t value) { return std::bit_cast<int32_t>(value); }
bool Unlocked(int32_t value) { return value == 1 || value == 2 || value == 3; }
std::optional<bool> IsUnlocked(const CgHistoryValue& value) {
  if (const auto* exact = std::get_if<int32_t>(&value)) return Unlocked(*exact);
  if (std::get<CgStatusSet>(value) == CgStatusSet::OneTwoOrThree) return true;
  return std::nullopt;
}
void CheckCgMetadata(const CgHistoryState& history) {
  if (!history.Known)
    throw UnsupportedCommand("initial CG history/required list is missing");
  if (history.Partial) {
    const auto& partial = *history.Partial;
    if (!history.Values.empty() || partial.InitialExtent > partial.Extent ||
        partial.Extent > 1000001)
      throw FormatError("inconsistent partial CG history");
    for (const auto& [id, value] : partial.Overrides) {
      (void)value;
      if (id >= partial.Extent)
        throw FormatError("CG override outside known extent");
    }
  }
  for (const auto id : history.Required)
    if (id < 0 || id > 1000000)
      throw FormatError("CG requirement outside host budget");
}
void ApplyCgView(CgHistoryState& history, int32_t id) {
  const auto status = CgHistoryStatus(history, id);
  const auto unlocked = IsUnlocked(status);
  if (unlocked && *unlocked) return;
  if (history.Partial) {
    auto& partial = *history.Partial;
    partial.Extent = std::max(partial.Extent, uint32_t(id) + 1);
    partial.Overrides[uint32_t(id)] =
        unlocked ? CgHistoryValue(int32_t(1))
                 : CgHistoryValue(CgStatusSet::OneTwoOrThree);
  } else {
    if (size_t(id) >= history.Values.size())
      history.Values.resize(size_t(id) + 1, history.Default);
    history.Values[size_t(id)] = 1;
  }
}
std::optional<bool> CgComplete(const CgHistoryState& history) {
  bool unknown = false;
  for (const auto id : history.Required) {
    const auto unlocked = IsUnlocked(CgHistoryStatus(history, id));
    if (unlocked && !*unlocked) return false;
    if (!unlocked) unknown = true;
  }
  return unknown ? std::nullopt : std::optional(true);
}
ImageSection BackgroundSection(int32_t kind) {
  if (kind == 2) return ImageSection::Background;
  if (kind == 3) return ImageSection::Event;
  if (kind == 4) return ImageSection::Free;
  throw UnsupportedCommand("unsupported rule-wipe background kind");
}
}  // namespace
size_t CgHistoryExtent(const CgHistoryState& history) {
  CheckCgMetadata(history);
  return history.Partial ? history.Partial->Extent : history.Values.size();
}
CgHistoryValue CgHistoryStatus(const CgHistoryState& history, int32_t id) {
  if (!history.Known)
    throw UnsupportedCommand("initial CG history/required list is missing");
  if (id < 0 || id > 1000000)
    throw FormatError("CG history exceeds host budget");
  if (history.Partial) {
    const auto& partial = *history.Partial;
    if (const auto found = partial.Overrides.find(uint32_t(id));
        found != partial.Overrides.end())
      return found->second;
    if (uint32_t(id) < partial.InitialExtent) return CgStatusSet::AnyI32;
    return history.Default;
  }
  return size_t(id) < history.Values.size() ? history.Values[size_t(id)]
                                            : history.Default;
}
CgHistoryState RestorePartialCgHistory(uint32_t extent, int32_t fallback,
                                       std::vector<int32_t> required) {
  if (extent > 1000000)
    throw FormatError("CG initial extent exceeds host budget");
  CgHistoryState result{true,
                        {},
                        std::move(required),
                        fallback,
                        CgPartialHistory{extent, extent, {}}};
  CheckCgMetadata(result);
  return result;
}
void ValidateCgHistory(const CgHistoryState& history, int32_t id) {
  CheckCgMetadata(history);
  if (id < 0 || id > 1000000)
    throw FormatError("CG history exceeds host budget");
  auto next = history;
  ApplyCgView(next, id);
  if (!CgComplete(next).has_value())
    throw UnsupportedCommand("CG completion trophy predicate is unknown");
}
void MarkCgViewed(SemanticState& state, int32_t id) {
  ValidateCgHistory(state.CgHistory, id);
  ApplyCgView(state.CgHistory, id);
  if (*CgComplete(state.CgHistory))
    state.PendingEvents.push_back({GameEventKind::Trophy, 29});
}
ScreenFilterState FreshScreenFilter() { return {0, 0.0f}; }
ScreenFilterCommand ScreenFilterCommand::Decode(std::span<const uint8_t> payload, bool normal) {
  PayloadReader reader(payload);
  return {normal, reader.I32()};
}
void StartScreenFilter(ScreenFilterState& state, ScreenFilterAnimation& animation,
                       const ScreenFilterCommand& command, uint64_t now) {
  int32_t from = 0;
  if (command.Normal) {
    if (!state.Intensity)
      throw UnsupportedCommand("restored screen filter intensity is unknown");
    const float value = *state.Intensity * 100.0f;
    if (!std::isfinite(value) || value >= 2147483648.0f || value < -2147483648.0f)
      throw FormatError("screen filter intensity exceeds integer range");
    from = int32_t(value);  // Native82ab0 vcvt.s32.f32, toward zero.
  }
  ScreenFilterAnimation next;
  next.Value.Start(from, command.Normal ? 0 : 100, command.Duration, now);
  next.Started = true;
  if (!command.Normal) state.Mode = 1;
  animation = next;
}
bool UpdateScreenFilter(ScreenFilterState& state, ScreenFilterAnimation& animation,
                        const ScreenFilterCommand& command, bool skipping, uint64_t now) {
  if (skipping) {
    state.Intensity = command.Normal ? 0.0f : 1.0f;
    if (command.Normal) state.Mode = 0;
    return true;
  }
  if (!animation.Started)
    throw UnsupportedCommand("screen filter Start was suppressed");
  animation.Value.Tick(now);
  const bool finished = animation.Value.Finished();
  if (finished && command.Normal) state.Mode = 0;
  state.Intensity = float(animation.Value.Current) / 100.0f;
  return finished;
}
ScreenFadeCommand ScreenFadeCommand::Decode(std::span<const uint8_t> payload,
                                            bool out) {
  PayloadReader reader(payload);
  return {out, reader.I32()};
}
void StartScreenFade(SemanticState& state, ScreenFadeAnimation& animation,
                     const ScreenFadeCommand& command, uint64_t now) {
  if (!state.Effects.Known)
    throw UnsupportedCommand("restored common effect context is missing");
  ScreenFadeAnimation next;
  next.Value.Start(command.Out ? 0 : 255, command.Out ? 255 : 0,
                   command.Duration, now);
  next.Started = true;
  state.ScreenFade = next.Value.From;
  state.Effects.CurrentValue = state.Effects.RestoreValue;
  for (uint32_t i = 0; i < 2; ++i)
    state.PendingEvents.push_back(
        {GameEventKind::StopChannel,
         Signed(uint32_t(state.Effects.FirstAudioChannel) + i)});
  animation = next;
}
bool UpdateScreenFade(SemanticState& state, ScreenFadeAnimation& animation,
                      const ScreenFadeCommand& command, bool skipping,
                      uint64_t now) {
  if (skipping) {
    state.ScreenFade = command.Out ? 255 : 0;
    return true;  // Native skip does not require a prior Start/timestamp.
  }
  if (!animation.Started)
    throw UnsupportedCommand("screen fade Start was suppressed");
  animation.Value.Tick(now);
  state.ScreenFade = animation.Value.Current;
  return animation.Value.Finished();
}
void AbsoluteLinearInterpolator::Start(int32_t from, int32_t to,
                                       int32_t duration, uint64_t now) {
  From = Current = from;
  To = to;
  Epoch = now;
  Deadline = now + uint64_t(int64_t(duration));
}
void AbsoluteLinearInterpolator::Tick(uint64_t now) {
  if (now >= Deadline) {
    Current = To;
    return;
  }
  const auto divisor = Signed(uint32_t(Deadline - Epoch));
  if (divisor == 0)
    throw FormatError("zero-length absolute interpolator before its epoch");
  const auto product =
      Signed((uint32_t(To) - uint32_t(From)) * uint32_t(now - Epoch));
  const auto quotient = int64_t(product) / divisor;
  Current = Signed(uint32_t(From) + uint32_t(quotient));
}
void AbsoluteLinearInterpolator::StartUnsigned(int32_t from, int32_t to,
                                               uint32_t duration,
                                               uint64_t now) {
  From = Current = from;
  To = to;
  Epoch = now;
  Deadline = now + duration;
}
QuakeCommand QuakeCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  QuakeCommand result;
  result.Selector = reader.U32();
  for (size_t i = 0; i < 2; ++i) {
    result.Amplitude[i] = reader.I32();
    result.LegDuration[i] = reader.U32();
  }
  result.Hold = reader.U32();
  result.Attack = reader.U32();
  result.Decay = reader.U32();
  return result;
}
void StartQuake(SemanticState& state, QuakeAnimation& animation,
                const QuakeCommand&, uint64_t now) {
  if (!state.Effects.Known)
    throw UnsupportedCommand("restored common effect context is missing");
  animation.Started = true;
  animation.Completed = false;
  animation.Stage = 0;
  animation.StartedAt = now;
  // Start intentionally preserves existing interpolators and output pairs.
  state.Effects.CurrentValue = state.Effects.RestoreValue;
  for (uint32_t i = 0; i < 2; ++i)
    state.PendingEvents.push_back(
        {GameEventKind::StopChannel,
         Signed(uint32_t(state.Effects.FirstAudioChannel) + i)});
}
bool UpdateQuake(SemanticState& state, QuakeAnimation& animation,
                 const QuakeCommand& command, bool skipping, uint64_t now) {
  if (skipping) {
    for (auto& pair : state.Quake) pair = std::array<int32_t, 2>{};
    animation.Completed = true;
    return true;  // Native skip has no Start/resource/timing dependency.
  }
  if (!animation.Started)
    throw UnsupportedCommand("quake Start was suppressed");
  if (animation.Completed) return true;
  auto& axes = animation.Axes;
  auto& envelope = animation.Envelope;
  auto scaled = [&] {
    for (size_t i = 0; i < 2; ++i) {
      auto& axis = axes[i];
      axis.Tick(now);
      if (axis.Current != axis.To) continue;
      // MUL wraps to U32; UMULL reciprocal and LSR6 divide that unsigned
      // product by100. Negative amplitudes do not use signed division here.
      const auto magnitude =
          (uint32_t(command.Amplitude[i]) * uint32_t(envelope.Current)) / 100u;
      const auto target = Signed(axis.Current < 0 ? magnitude : 0u - magnitude);
      axis.StartUnsigned(axis.Current, target, command.LegDuration[i], now);
    }
  };
  // Request(...,1) changes the current ScriptState immediately. Separate ifs
  // preserve same-update phase fallthrough, including zero-duration envelopes.
  if (animation.Stage == 0) {
    envelope.StartUnsigned(0, 100, command.Attack, now);
    for (size_t i = 0; i < 2; ++i)
      axes[i].StartUnsigned(0, 0, command.LegDuration[i], now);
    animation.Stage = 1;
  }
  if (animation.Stage == 1) {
    envelope.Tick(now);
    if (envelope.Current == envelope.To) animation.Stage = 2;
    scaled();
  }
  if (animation.Stage == 2) {
    envelope.StartUnsigned(0, 100, command.Hold, now);
    animation.Stage = 3;
  }
  if (animation.Stage == 3) {
    envelope.Tick(now);
    if (envelope.Current == envelope.To) animation.Stage = 4;
    for (size_t i = 0; i < 2; ++i) {
      auto& axis = axes[i];
      axis.Tick(now);
      if (axis.Current == axis.To)
        axis.StartUnsigned(axis.Current, Signed(0u - uint32_t(axis.Current)),
                           command.LegDuration[i], now);
    }
  }
  if (animation.Stage == 4) {
    envelope.StartUnsigned(100, 0, command.Decay, now);
    animation.Stage = 5;
  }
  if (animation.Stage == 5) {
    envelope.Tick(now);
    if (envelope.Current == envelope.To) animation.Stage = 7;
    scaled();
  }
  if (animation.Stage == 6) {
    // No incoming state6 assignment is present in this command's lifecycle.
    for (auto& axis : axes)
      axis.StartUnsigned(axis.Current, 0, command.LegDuration[0], now);
    animation.Stage = 7;
  }
  if (animation.Stage == 7) {
    for (auto& axis : axes) axis.Tick(now);
    if (axes[0].Current == axes[0].To && axes[1].Current == axes[1].To) {
      animation.Stage = 0;
      for (auto& axis : axes) axis.StartUnsigned(0, 0, 0, 0);
      for (auto& pair : state.Quake) pair = std::array<int32_t, 2>{};
      animation.Completed = true;
    }
  }
  const std::array<int32_t, 2> pair{axes[0].Current, axes[1].Current};
  const auto selector = command.Selector;
  if (selector == 0 || selector == 2 || selector == 4) state.Quake[0] = pair;
  if (selector == 1 || selector == 2 || selector == 4) state.Quake[1] = pair;
  if (selector == 3 || selector == 4) state.Quake[2] = pair;
  return animation.Completed;
}
RuleWipeDraw BuildRuleWipeDraw(const RuleWipeState& state) {
  if (!state.Known)
    throw UnsupportedCommand("restored rule-wipe drawing inputs are missing");
  RuleWipeDraw draw;
  if (!state.Active && !state.Auxiliary) return draw;
  if (!state.Progress)
    throw UnsupportedCommand("rule-wipe drawing progress is missing");
  draw.Destination = draw.Source = {0, 0, 960, 544};
  draw.Background = state.BackgroundResource;
  draw.Color = draw.Background ? std::array<uint8_t, 4>{255, 255, 255, 255}
                               : state.Color;
  if (state.Active) {
    if (state.CachedRule < 0)
      throw UnsupportedCommand("active rule-wipe drawing resource is missing");
    draw.Kind = draw.Background ? RuleWipeDrawKind::MaskedImage
                               : RuleWipeDrawKind::MaskedSolid;
    draw.Rule = ImageKey{ImageSection::Rule, uint32_t(state.CachedRule)};
    draw.Value = float(*state.Progress) / 100.0f;
  } else {
    draw.Kind = draw.Background ? RuleWipeDrawKind::Image
                               : RuleWipeDrawKind::Solid;
    // Preserve native I32 wrap, signed division toward zero and byte narrowing.
    const auto numerator = Signed(uint32_t(*state.Progress) * 255u + 25500u);
    draw.Color[3] = uint8_t(numerator / 200);
    draw.EnableAlphaTest = draw.Background.has_value();
  }
  return draw;
}
RuleWipeCommand RuleWipeCommand::Decode(std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  RuleWipeCommand result{reader.I32(), reader.I32(), reader.I32(),
                         reader.I32(), reader.I32(), {}};
  if (result.BackgroundKind != 1) result.ColorOrImage = reader.I32();
  return result;  // Record alignment tail is not a serialized field.
}
void StartRuleWipe(SemanticState& state, RuleWipeAnimation& animation,
                   const RuleWipeCommand& command, SceneImageDriver& images,
                   uint64_t now) {
  if (!state.Effects.Known || !state.RuleWipe.Known)
    throw UnsupportedCommand(
        "restored rule-wipe/common effect context is missing");
  if (command.RuleId < 0 || command.BackgroundKind < 0 ||
      command.BackgroundKind > 4 ||
      (command.BackgroundKind != 1 && !command.ColorOrImage))
    throw FormatError("invalid rule-wipe request");
  auto next = state.RuleWipe;
  RuleWipeAnimation runtime;
  runtime.Started = true;
  runtime.RuleReused = next.CachedRule == command.RuleId;
  runtime.BackgroundReused = command.BackgroundKind >= 2 &&
                             next.CachedBackground == *command.ColorOrImage;
  if (command.BackgroundKind >= 2 && *command.ColorOrImage < 0)
    throw FormatError("negative rule-wipe background image");
  const bool cg = command.BackgroundKind == 3 && !runtime.BackgroundReused;
  if (cg) ValidateCgHistory(state.CgHistory, *command.ColorOrImage);
  if (!runtime.RuleReused) {
    images.Prepare(ImageSlot::Rule,
                   {ImageSection::Rule, uint32_t(command.RuleId)});
    if (next.Active) next.Auxiliary = true;
    next.Active = false;
  }
  if (command.BackgroundKind >= 2 && !runtime.BackgroundReused) {
    const ImageKey key{BackgroundSection(command.BackgroundKind),
                       uint32_t(*command.ColorOrImage)};
    images.Prepare(ImageSlot::Background, key);
    if (next.Active) next.Auxiliary = true;
    next.BackgroundResource = key;
    next.CachedBackground = *command.ColorOrImage;
  }
  next.Direction = command.Direction;
  next.Parameter = command.Parameter;
  next.BackgroundKind = command.BackgroundKind;
  next.CachedRule = command.RuleId;
  if (command.Direction != 0)
    next.Progress = 100;
  else
    runtime.Progress.Start(-100, 100, command.Duration, now);
  if (command.BackgroundKind == 0) {
    const auto packed = uint32_t(*command.ColorOrImage);
    next.Color = {uint8_t(packed >> 16), uint8_t(packed >> 8), uint8_t(packed),
                  uint8_t(packed >> 24)};
  }
  state.Effects.CurrentValue = state.Effects.RestoreValue;
  for (uint32_t i = 0; i < 2; ++i)
    state.PendingEvents.push_back(
        {GameEventKind::StopChannel,
         Signed(uint32_t(state.Effects.FirstAudioChannel) + i)});
  if (cg) MarkCgViewed(state, *command.ColorOrImage);
  state.RuleWipe = next;
  animation = runtime;
}
bool UpdateRuleWipe(SemanticState& state, RuleWipeAnimation& animation,
                    const RuleWipeCommand& command, SceneImageDriver& images,
                    bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand("rule-wipe Start was suppressed");
  if (animation.Completed) return true;
  auto& wipe = state.RuleWipe;
  if (animation.Stage == 0) {
    if (!images.Ready()) return false;
    const bool forward = wipe.Direction == 0;
    animation.Progress.Start(forward ? -100 : 100, forward ? 100 : -100,
                             command.Duration, now);
    wipe.Progress = animation.Progress.Current;
    wipe.Active = true;
    animation.Stage = 1;  // Deferred state1: do not run it in this same call.
    return false;
  }
  if (!skipping && animation.Progress.Current != animation.Progress.To) {
    animation.Progress.Tick(now);
    wipe.Progress = animation.Progress.Current;
    return false;  // Native checks endpoint before ticking, even at deadline.
  }
  wipe.Progress = animation.Progress.To;
  animation.Completed = true;
  if (wipe.Direction == 1) {
    images.Release(ImageSlot::Rule);
    images.Release(ImageSlot::Background);
    wipe.CachedRule = wipe.CachedBackground = -1;
    wipe.BackgroundResource.reset();
    wipe.Active = wipe.Auxiliary = false;
  }
  return true;
}
}  // namespace Impacto::Saekano
