#pragma once
#include "approachresult.h"
#include "textreveal.h"

namespace Impacto::Saekano {
struct ScriptVoiceEnvelope;
class GameResources;
class ApproachMissionAudio;
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
// Native result18f9c ->18750: black body, outer opacity255, shadow flag cleared
// by Start80fe8. The result badge's text-alpha tween is a separate draw.
std::vector<UiTextBlit> BuildApproachResultTextDraws(const ApproachResultText& text,
    const TextFormatEnvironment& format,std::array<int32_t,2> origin,uint32_t utilitySize);
class ArchiveApproachResultHost final : public ApproachResultHost {
 public:
  ArchiveApproachResultHost(GameResources& resources,ApproachMissionAudio& audio,
      SkipAutoState& policy,const TextFormatEnvironment& format,const TextTimingEnvironment& timing,
      ModeResultVisualEnvironment visual,const GsfFont& font,TextLayoutMetrics metrics,
      uint32_t utilitySize,uint32_t nominalSize);
  void Validate(const SemanticState& owner) const override;
  void Tick(uint64_t now) override;
  bool Revealed(uint64_t now) override { return text_.Update(now); }
  void Act(const ApproachResultRequest& request,const ApproachResultState& result,
           SemanticState& owner,uint64_t now) override;
  const ApproachResultText& Text() const { return text_; }
 private:
  GameResources& resources_;ApproachMissionAudio& audio_;SkipAutoState& policy_;
  const TextFormatEnvironment& format_;const TextTimingEnvironment& timing_;
  ModeResultVisualEnvironment visual_;const GsfFont& font_;TextLayoutMetrics metrics_;
  uint32_t utility_,nominal_;ApproachResultText text_;
};
}
