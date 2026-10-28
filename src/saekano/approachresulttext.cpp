#include "approachresulttext.h"
#include "resources.h"
#include <algorithm>

namespace Impacto::Saekano {
void ApproachResultText::Prepare(const ApproachResultCommand::Outcome& outcome,
    std::span<const ScriptVoiceEnvelope> catalog,const TextFormatEnvironment& format,
    const ModeResultVisualEnvironment& environment,const GsfFont& font,
    const TextLayoutMetrics& metrics,uint32_t utilitySize,uint32_t nominalSize,uint64_t now) {
  ApproachResultText next;next.Voice=outcome.Voice;
  if(outcome.Voice>=0) {
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& row){return row.VoiceId==outcome.Voice;});
    if(found==catalog.end() || found->Mouth.Duration<0)
      throw UnsupportedCommand("result voice duration is unavailable");
    next.VoiceDuration=uint32_t(found->Mouth.Duration);
  } else if(outcome.Voice!=-1)throw FormatError("invalid result voice ID");
  next.Reveal.PrepareUtf16(outcome.Text,format,now,true);
  next.Layout=LayoutPlainText(next.Reveal.Text,format,font,metrics,environment.TextWidth,
                              environment.TextScale,utilitySize,nominalSize);
  if(next.Layout.Glyphs.size()!=next.Reveal.Glyphs.size())
    throw UnsupportedCommand("result glyph layout and reveal disagree");
  *this=std::move(next);
}
void ApproachResultText::Begin(const TextTimingEnvironment& timing,uint8_t speed,uint64_t now) {
  if(Voice>=0) {
    if(!VoiceDuration)throw UnsupportedCommand("result voice duration was not prepared");
    Reveal.Start(timing.ForApproachResult(*VoiceDuration,speed),now);
  } else Reveal.StartCharacters(timing.ForCharacters(speed),now);
}
bool ApproachResultText::Update(uint64_t now) {
  // Native result UpdateReveal always receives-1, not the live audio position.
  Reveal.Update(now,-1);return Reveal.Revealed;
}
void ApproachResultText::RevealAll(uint64_t now) { Reveal.Reveal(now); }
}
