#pragma once

#include "image.h"
#include "gauge.h"

#include <map>
#include <variant>

namespace Impacto::Saekano {
struct SemanticState;
struct ScreenFadeCommand {
  bool Out = false;
  int32_t Duration = 0;
  static ScreenFadeCommand Decode(std::span<const uint8_t> payload, bool out);
};
struct ScreenFadeAnimation {
  bool Started = false;
  ScriptLinearTween Value;
};
void StartScreenFade(SemanticState& state, ScreenFadeAnimation& animation,
                     const ScreenFadeCommand& command, uint64_t now);
bool UpdateScreenFade(SemanticState& state, ScreenFadeAnimation& animation,
                      const ScreenFadeCommand& command, bool skipping,
                      uint64_t now);
struct AbsoluteLinearInterpolator {
  int32_t From = 0, To = 0, Current = 0;
  uint64_t Epoch = 0, Deadline = 0;
  void Start(int32_t from, int32_t to, int32_t duration, uint64_t now);
  void StartUnsigned(int32_t from, int32_t to, uint32_t duration, uint64_t now);
  void Tick(uint64_t now);
};
struct QuakeCommand {
  uint32_t Selector = 0;
  std::array<int32_t, 2> Amplitude{};
  std::array<uint32_t, 2> LegDuration{};
  uint32_t Hold = 0, Attack = 0, Decay = 0;
  static QuakeCommand Decode(std::span<const uint8_t> payload);
};
struct QuakeAnimation {
  bool Started = false, Completed = false;
  uint32_t Stage = 0xf0000000;
  uint64_t StartedAt = 0;
  std::array<AbsoluteLinearInterpolator, 2> Axes;
  AbsoluteLinearInterpolator Envelope;
};
// C20dc/e0, C20e4/e8, C20ec/f0; renderer layer identities remain unproved.
// Omitted restored words stay unknown until an actual command writes them.
using QuakeOffsets = std::array<std::optional<std::array<int32_t, 2>>, 3>;
void StartQuake(SemanticState& state, QuakeAnimation& animation,
                 const QuakeCommand& command, uint64_t now);
bool UpdateQuake(SemanticState& state, QuakeAnimation& animation,
                  const QuakeCommand& command, bool skipping, uint64_t now);
struct EffectContext {
  bool Known = false;  // Explicit initial common fields and idle resource gate.
  int32_t FirstAudioChannel = 0;
  int32_t RestoreValue = 0,
          CurrentValue = 0;  // C160c/1610; consumer meaning unproved.
};
enum class CgStatusSet { AnyI32, OneTwoOrThree };
using CgHistoryValue = std::variant<int32_t, CgStatusSet>;
struct CgPartialHistory {
  uint32_t InitialExtent = 0, Extent = 0;
  // Absent initial entries are unknown. New slots are implicitly Default;
  // overrides retain exact values or a proved set, never invented zeros.
  std::map<uint32_t, CgHistoryValue> Overrides;
};
struct CgHistoryState {
  bool Known = false;
  std::vector<int32_t> Values, Required;
  int32_t Default = 0;
  std::optional<CgPartialHistory> Partial;
};
size_t CgHistoryExtent(const CgHistoryState& history);
CgHistoryValue CgHistoryStatus(const CgHistoryState& history, int32_t id);
CgHistoryState RestorePartialCgHistory(uint32_t extent, int32_t fallback,
                                       std::vector<int32_t> required);
void ValidateCgHistory(const CgHistoryState& history, int32_t id);
void MarkCgViewed(SemanticState& state, int32_t id);
struct RuleWipeState {
  bool Known = false;
  bool Active = false, Auxiliary = false;  // C1be9, C1be8.
  int32_t Direction = 0, Parameter = 0, BackgroundKind = 0;
  int32_t CachedRule = -1, CachedBackground = -1;
  std::optional<int32_t>
      Progress;  // Inactive native contexts may leave this uninitialized.
  std::array<uint8_t, 4> Color{};
  std::optional<ImageKey> BackgroundResource;
};
struct RuleWipeCommand {
  int32_t Direction, Parameter, Duration, RuleId, BackgroundKind;
  std::optional<int32_t> ColorOrImage;
  static RuleWipeCommand Decode(std::span<const uint8_t> payload);
};
struct RuleWipeAnimation {
  bool Started = false, Completed = false;
  uint32_t Stage = 0;
  bool RuleReused = false, BackgroundReused = false;
  AbsoluteLinearInterpolator Progress;
};
void StartRuleWipe(SemanticState& state, RuleWipeAnimation& animation,
                   const RuleWipeCommand& command, SceneImageDriver& images,
                   uint64_t now);
bool UpdateRuleWipe(SemanticState& state, RuleWipeAnimation& animation,
                    const RuleWipeCommand& command, SceneImageDriver& images,
                    bool skipping, uint64_t now);
}  // namespace Impacto::Saekano
