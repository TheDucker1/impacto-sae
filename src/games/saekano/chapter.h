#pragma once

#include "gauge.h"

namespace Impacto::Saekano {
struct SemanticState;
struct SavedDayTime {
  uint8_t Enabled = 0;
  std::array<int32_t, 2> Components{};
};
struct ChapterEnvironment {
  int32_t Duration = 0;
  size_t PrefixBytes = 0;
  static ChapterEnvironment Decode(std::span<const uint8_t> bytes);
};
struct ChapterTitleCommand {
  int32_t Id = 0;
  std::array<std::u16string, 2> Titles;
  static ChapterTitleCommand Decode(std::span<const uint8_t> bytes);
};
struct ChapterTitleAnimation {
  bool Started = false, Completed = false;
  uint32_t Stage = 0;
  std::optional<uint32_t> PendingStage;
  ScriptLinearTween Fade, Hold;
};
void StartChapterTitle(SemanticState& state, ChapterTitleAnimation& animation,
                       const ChapterTitleCommand& command,
                       const ChapterEnvironment& environment, uint64_t now);
bool UpdateChapterTitle(SemanticState& state, ChapterTitleAnimation& animation,
                        const ChapterEnvironment& environment, bool advance,
                        uint64_t now);
}  // namespace Impacto::Saekano
