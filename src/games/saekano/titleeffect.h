#pragma once
#include "title.h"
#include "gauge.h"
#include "random.h"

namespace Impacto::Saekano {
struct TitleEffectParticle {
  std::array<float,2> Position{-500,-500};
  uint32_t ColorIndex=0;
  ScriptLinearTween Scale,Alpha,NextAlpha,Hold;
  bool Shrinking=false;
  int32_t Sequence=0;
};
struct TitleEffectState {
  bool Started=false;
  int32_t Variant=0,Phase=0;
  std::optional<int32_t> PendingPhase;
  ScriptLinearTween Spawn;
  std::array<TitleEffectParticle,5> Particles;
  GameRandom Random;
};
// Separate title decoration, not the scenario particle manager or its RNG.
void StartTitleEffects(TitleEffectState& state,const TitleEnvironment& env,uint64_t now);
void ResetTitleEffects(TitleEffectState& state,const TitleEnvironment& env,int32_t variant,uint64_t now);
void UpdateTitleEffects(TitleEffectState& state,const TitleEnvironment& env,uint64_t now);
std::vector<UiSpriteBlit> BuildTitleEffects(const TitleEffectState& state,const TitleEnvironment& env);
}
