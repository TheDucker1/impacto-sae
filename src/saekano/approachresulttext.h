#pragma once
#include "approachresult.h"
#include "textreveal.h"

namespace Impacto::Saekano {
struct ScriptVoiceEnvelope;
// Result text has its own lifetime, independent of the last ordinary message.
struct ApproachResultText {
  VoiceTextReveal Reveal;
  PlainTextLayout Layout;
  int32_t Voice=-1;
  std::optional<uint32_t> VoiceDuration;
  void Prepare(const ApproachResultCommand::Outcome& outcome,
               std::span<const ScriptVoiceEnvelope> catalog,
               const TextFormatEnvironment& format,
               const ModeResultVisualEnvironment& environment,
               const GsfFont& font,const TextLayoutMetrics& metrics,
               uint32_t utilitySize,uint32_t nominalSize,uint64_t now);
  void Begin(const TextTimingEnvironment& timing,uint8_t speed,uint64_t now);
  bool Update(uint64_t now);
  void RevealAll(uint64_t now);
};
}
