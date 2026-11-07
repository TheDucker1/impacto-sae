#include "texttiming.h"

#include "commands.h"

#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
void CheckFactor(float factor) {
  if (!std::isfinite(factor) || factor < 0)
    throw FormatError("invalid text timing factor");
}
uint32_t Duration(uint32_t duration, float factor) {
  CheckFactor(factor);
  // Match separate VCVT.F32.U32, VMUL.F32 and truncating VCVT.U32.F32.
  // The last instruction saturates; a C++ out-of-range float cast is undefined.
  const float product = float(duration) * factor;
  if (product >= 4294967296.0f) return std::numeric_limits<uint32_t>::max();
  if (product <= 0) return 0;
  return uint32_t(product);
}
}  // namespace
TextTimingEnvironment TextTimingEnvironment::Decode(
    std::span<const uint8_t> bytes, bool readIntervals, bool readAutoDelays) {
  if (bytes.size() < 3) throw FormatError("truncated text timing prefix");
  TextTimingEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  const auto count = reader.U32();
  if (count < 3 || count > 4096)
    throw FormatError("text timing factor count outside supported bounds");
  reader.CheckCount(count, 4);
  result.Factors.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto factor = std::bit_cast<float>(reader.U32());
    CheckFactor(factor);
    result.Factors.push_back(factor);
  }
  result.PrefixBytes = bytes.size() - reader.Remaining();
  if (readIntervals || readAutoDelays) {
    const auto intervals = reader.U32();
    reader.CheckCount(intervals, 4);
    if (!intervals || intervals > 4096)
      throw FormatError("invalid character interval count");
    for (uint32_t i = 0; i < intervals; ++i)
      result.Intervals.push_back(reader.U32());
    result.IntervalPrefixBytes = bytes.size() - reader.Remaining();
  }
  if (readAutoDelays) {
    const auto delays = reader.U32();
    reader.CheckCount(delays, 4);
    if (!delays || delays > 4096)
      throw FormatError("invalid auto delay count");
    for (uint32_t i = 0; i < delays; ++i)
      result.AutoDelays.push_back(reader.I32());
    result.AutoDelayPrefixBytes = bytes.size() - reader.Remaining();
  }
  return result;
}
int32_t TextTimingEnvironment::ForAutoDelay(uint8_t option) const {
  if (option >= AutoDelays.size())
    throw FormatError("auto speed option outside delay table");
  return AutoDelays[option];
}
bool MessageAutoReady(const TextTimingEnvironment& environment,
                      const MessageAutoTiming& message, uint64_t now) {
  if (!message.AutoEnabled || !message.PreviousUpdated || !message.TextRevealed)
    return false;
  uint32_t duration;
  if (message.VoiceId >= 0) {
    if (!message.VoiceDuration)
      throw FormatError("message auto timing needs native voice duration");
    duration = *message.VoiceDuration;
  } else {
    duration = environment.ForCharacters(message.TextSpeed) * message.SourceUnits;
  }
  const uint64_t deadline =
      message.Epoch + uint64_t(duration) +
      uint64_t(int64_t(environment.ForAutoDelay(message.AutoSpeed)));
  return now >= deadline;
}
uint32_t TextTimingEnvironment::ForCharacters(uint8_t option) const {
  if (option >= Intervals.size())
    throw FormatError("text speed option outside interval table");
  return Intervals[option];
}
VoiceTextDurations TextTimingEnvironment::ForVoice(uint32_t duration,
                                                   uint8_t option) const {
  if (Factors.size() < 3 || option >= Factors.size())
    throw FormatError("text speed option outside authored table");
  return {Duration(duration, Factors[option]), Duration(duration, Factors[2])};
}
VoiceTextDurations TextTimingEnvironment::ForApproachResult(uint32_t duration,uint8_t option) const {
  if(option>=Factors.size())throw FormatError("result text speed outside authored table");
  return {duration,Duration(duration,Factors[option])};
}
}  // namespace Impacto::Saekano
