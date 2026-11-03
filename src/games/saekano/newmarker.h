#pragma once
#include "titlecontroller.h"

namespace Impacto::Saekano {
// Native env_global c5e8..c62c. Six-word records are x,y,u,v,width,height.
struct NewMarkerEnvironment {
  std::array<int32_t,6> Base{};
  std::vector<std::array<int32_t,6>> Particles;
  std::array<int32_t,3> Scale{};
  std::array<uint32_t,3> ScaleDuration{};
  uint32_t SpawnDuration=0,CycleDuration=0;
  size_t PrefixBytes=0;
  static NewMarkerEnvironment Decode(std::span<const uint8_t> bytes);
};
struct NewMarkerParticle {
  int32_t Phase=4;
  std::optional<int32_t> PendingPhase;
  TitleTween Scale;
};
// One owner per screen, updated once per frame and drawn at each marked row.
struct NewMarkerState {
  int32_t Phase=0,Index=0;
  std::optional<int32_t> PendingPhase;
  std::array<NewMarkerParticle,4> Particles;
  TitleTween Spawn,Cycle;
};
NewMarkerState StartNewMarker(const NewMarkerEnvironment& environment,uint64_t now);
void UpdateNewMarker(NewMarkerState& state,const NewMarkerEnvironment& environment,uint64_t now);
std::array<UiSpriteBlit,5> BuildNewMarker(const NewMarkerState& state,
    const NewMarkerEnvironment& environment,ProfileTextureKey texture,
    std::array<int32_t,2> anchor);
}
