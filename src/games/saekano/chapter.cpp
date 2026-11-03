#include "chapter.h"
#include "commands.h"

#include <bit>

namespace Impacto::Saekano {
ChapterEnvironment ChapterEnvironment::Decode(std::span<const uint8_t> bytes) {
  size_t cursor = 0;
  const auto skip = [&](size_t count) {
    if (count > bytes.size() - cursor)
      throw FormatError("truncated script environment prefix");
    cursor += count;
  };
  const auto word = [&]() {
    const auto at = cursor;
    skip(4);
    return uint32_t(bytes[at]) | uint32_t(bytes[at+1]) << 8 |
           uint32_t(bytes[at+2]) << 16 | uint32_t(bytes[at+3]) << 24;
  };
  const auto counted = [&](size_t stride) {
    const auto count = word();
    if (count > 1000000 || count > (bytes.size() - cursor) / stride)
      throw FormatError("script environment count exceeds available data");
    skip(size_t(count) * stride);
  };
  // Reader801da330. Descriptor801c23d0 consumes I16, seven words and RGBA.
  skip(3 + 4*4 + 34);
  counted(34);
  counted(4);
  skip(2*34);
  counted(8);
  skip(34 + 5*4);
  counted(2);  // UTF16 string reader801c2824.
  skip(3*4 + 2*8 + 4);
  const auto duration = std::bit_cast<int32_t>(word());
  if (duration < 0) throw FormatError("negative chapter transition duration");
  return {duration, cursor};
}
ChapterTitleCommand ChapterTitleCommand::Decode(std::span<const uint8_t> bytes) {
  PayloadReader reader(bytes);
  return {reader.I32(), {reader.String(), reader.String()}};
}
void StartChapterTitle(SemanticState& state, ChapterTitleAnimation& animation,
                       const ChapterTitleCommand& command,
                       const ChapterEnvironment& environment, uint64_t now) {
  if (environment.Duration < 0)
    throw FormatError("negative chapter transition duration");
  animation = {};
  animation.Started = true;
  animation.Fade.Start(255, 0, environment.Duration, now);
  state.TitleDisplay = true;
  state.ChapterId = command.Id;
  state.Titles.assign(command.Titles.begin(), command.Titles.end());
  state.SavedChapter = command;
  // Direct service stops map logical SE0/1 to backend2/3. Unlike ScriptStopSe,
  // these do not invalidate the context's remembered resume track C15ac.
  state.PendingEvents.push_back({GameEventKind::StopSe, 2});
  state.PendingEvents.push_back({GameEventKind::StopSe, 3});
}
bool UpdateChapterTitle(SemanticState& state, ChapterTitleAnimation& animation,
                        const ChapterEnvironment& environment, bool advance,
                        uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand("chapter title Start was suppressed");
  if (animation.PendingStage) {
    animation.Stage = *animation.PendingStage;
    animation.PendingStage.reset();
  }
  if (animation.Stage == 0) {
    animation.Fade.Start(255, 0, environment.Duration, now);
    animation.Stage = 1;
  }
  if (animation.Stage == 1) {
    animation.Fade.Tick(now);
    if (animation.Fade.Finished()) {
      animation.Hold.Start(0, 1000, environment.Duration, now);
      animation.Stage = 2;
    }
  }
  if (animation.Stage == 2) {
    animation.Hold.Tick(now);
    // Native queues phase3; it is consumed at the next update, including skip.
    if (animation.Hold.Finished() || advance) animation.PendingStage = 3;
  }
  if (animation.Stage == 3) {
    animation.Fade.Start(0, 255, environment.Duration, now);
    animation.Stage = 4;
  }
  if (animation.Stage == 4) {
    animation.Fade.Tick(now);
    if (animation.Fade.Finished()) animation.Stage = 5;
  }
  if (animation.Stage == 5) {
    state.TitleDisplay = false;
    animation.Completed = true;
  }
  state.ScreenFade = animation.Fade.Current;
  return animation.Completed;
}
}  // namespace Impacto::Saekano
