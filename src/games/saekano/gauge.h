#pragma once

#include "live2dstate.h"

namespace Impacto::Saekano {
struct SemanticState;
struct UiSpriteDescriptor {
  int16_t Resource = 0;
  std::array<int32_t, 8> Words{};
};

// The non-repeating 56-byte ScriptTween path used by the gauge commands.
// Distinct from the 40-byte model parameter tween. Native arithmetic wraps its
// 32-bit product before signed division and clamps the sample at the deadline.
struct ScriptLinearTween {
  int32_t From = 0, To = 0, Current = 0;
  uint32_t Duration = 0;
  uint64_t Epoch = 0, Sample = 0;
  void Start(int32_t from, int32_t to, int32_t duration, uint64_t now);
  void StartUnsigned(int32_t from, int32_t to, uint32_t duration, uint64_t now);
  void Tick(uint64_t now);
  bool Finished() const;
};
// Native9fb60/9fc30 reversing channel. Result items use limit1; Options uses-1. The reverse
// restarts at the real clock, carries the native endpoint-based remainder,
// and increments its counter after the recursive advance has returned.
struct ScriptReversingTween {
  ScriptLinearTween Channel;
  bool Enabled=false;
  uint32_t Reversals=0;
  int32_t RepeatLimit=1; // -1 repeats indefinitely and resets the native counter.
  bool ReverseAtEnd=true; //9fb1a restarts at From;9fb60 reverses the endpoints.
  void Configure(int32_t from,int32_t to,int32_t duration,bool enabled,uint64_t now,int32_t repeatLimit=1,bool reverseAtEnd=true);
  void Restart(uint64_t now);
  void Tick(uint64_t now);
  bool Finished()const{return Channel.Finished();}
 private:
  void Advance(int32_t delta,uint64_t clock,size_t depth);
};

struct BattleGaugeEnvironment {
  int32_t Minimum = 0, NeutralThreshold = 0, Duration = 0;
  int32_t DisplayPerPoint = 0, XPerPoint = 0;
  float ScalePerPoint = 0;
  static BattleGaugeEnvironment Decode(std::span<const uint8_t> bytes);
};

struct BattleGaugeState {
  // SAS1 omitted these fields. A fresh mode must initialize them; restored
  // callers must provide the actual initial capture, not infer from score.
  bool Known = false;
  int32_t Display = 0;  // C+14ad8, separate from logical C+14a58.
  std::array<int32_t, 2> SideCharacters{};  // C+20b4/20bc.
  std::array<int32_t, 2> Standings{};       // C+14ec0/14ec4.
};
struct BattleGaugeCommand {
  int32_t Character = 0, Delta = 0;
  static BattleGaugeCommand Decode(std::span<const uint8_t> payload);
};
struct BattleGaugeAnimation {
  struct Actor {
    ScriptLinearTween X, Scale, Y;
    int32_t BaselineY = 0;
  };
  bool Started = false;
  int32_t ActualDelta = 0;
  ScriptLinearTween Display;
  std::array<std::optional<Actor>, 2> Actors;
};

void StartBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                      const BattleGaugeCommand& command,
                      const BattleGaugeEnvironment& environment,
                      Live2dDriver* models, uint64_t now);
bool UpdateBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                       Live2dDriver* models, bool skipping, uint64_t now);

struct ApproachGaugeEnvironment {
  int32_t Duration = 0, ParticleMinimumDuration = 0,
          ParticleMaximumDuration = 0;
  size_t ParticleCount = 0;
  std::array<int32_t, 4>
      ParticleRegion{};  // x,y,width,height of configured gauge sprite.
  std::array<int32_t, 2> PositiveEffectPosition{}, NegativeEffectPosition{};
  UiSpriteDescriptor ParticleSprite;
  static ApproachGaugeEnvironment Decode(std::span<const uint8_t> bytes);
};
// Profile and approach use the same result animation configuration, loaded
// from env_approach rather than their respective scene data.
struct ModeResultEnvironment {
  int32_t Progress = 0, HorizontalSlide = 0, HorizontalStart = 0,
          HorizontalOpacity = 0, VerticalSlide = 0, VerticalStart = 0,
          VerticalOpacity = 0, SecondaryProgress = 0, TextOpacity = 0;
  size_t PrefixBytes = 0;
  static ModeResultEnvironment DecodeApproach(std::span<const uint8_t> bytes);
};
struct ApproachParticle {
  ScriptLinearTween Opacity;
  std::optional<std::array<float, 2>> Position;
};
struct ApproachGaugeState {
  bool Known = false;
  bool Changing = false;  // C+1451c byte.
  std::vector<ApproachParticle>
      Particles;  // C+14524 ordered64-byte (0x40) native rows.
};
struct ApproachGaugeCommand {
  int32_t Delta =
      0;  // Start mutates this field; retained on same-command reentry.
  static ApproachGaugeCommand Decode(std::span<const uint8_t> payload);
};
struct ApproachGaugeAnimation {
  bool Started = false, Completed = false;
  int32_t Initial = 0, Target = 0;
  ScriptLinearTween Progress;
};
void StartApproachGauge(SemanticState& state, ApproachGaugeAnimation& animation,
                        ApproachGaugeCommand& command,
                        const ApproachGaugeEnvironment& environment,
                        uint64_t now);
bool UpdateApproachGauge(SemanticState& state,
                         ApproachGaugeAnimation& animation,
                         const ApproachGaugeCommand& command,
                         const ApproachGaugeEnvironment& environment,
                         bool skipping, uint64_t now);
}  // namespace Impacto::Saekano
