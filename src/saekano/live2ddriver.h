#pragma once

#include "live2denv.h"
#include "live2dsdk.h"
#include "resources.h"

#include <map>

namespace Impacto::Saekano {
// Observation of game-owned runtime, separate from VM record target fields.
// This is not a complete save format: SDK queues/blink internals are not saved.
struct Live2dDriverObservation {
  std::array<float, 2> Position{}, Scale{};
  std::array<int32_t, 2> Anchor{};
  std::array<uint8_t, 4> Color{};
  std::array<int32_t, 4> EyeCurrent{}, EyeTarget{};
  bool Enabled = false, OneShotPending = false, OneShotActive = false;
  bool SweatRamp = false, TearSelected = false, TearStopping = false;
  bool MouthActive = false;
  float Mouth = 0;
  std::optional<size_t> Loop, QueuedLoop;
  std::optional<size_t> Expression;
};

// Saekano's game wrapper around the original Cubism2 SDK/framework. Resource
// preparation is synchronous on the owning thread; successful ResourceReady
// means parsing has actually completed, not merely that a request was queued.
class SdkLive2dDriver final : public Live2dDriver {
 public:
  SdkLive2dDriver(GameResources& resources, Live2dEnvironment environment);
  ~SdkLive2dDriver();
  bool Supports(uint32_t selector) const override;
  bool GateReady() const override { return gateReady_; }
  void SetGateReady(bool ready) { gateReady_ = ready; }
  bool ResourceReady(int32_t resource) const override;
  void RequestResource(int32_t resource, int32_t logical,
                       uint64_t now) override;
  void Attach(const Live2dRecord& record, int32_t logical,
              uint64_t now) override;
  void Apply(const Live2dRecord& record, const Live2dCommand& command,
             uint64_t now) override;
  void Remove(const Live2dRecord& record) override;
  void Reset() override;
  // Recreates the native game's SAVE/LOAD behavior; this deliberately starts
  // fresh queues/tweens, rather than transplanting an arbitrary live SDK state.
  // Prepare transactionally so a bad save cannot destroy the current scene.
  void Restore(const Live2dState& state, uint64_t now);

  // Called exactly once per game frame, independently of VM Step boundaries.
  // Invisible attached models still animate. No OpenGL context is needed here.
  void Frame(const Live2dState& state, uint64_t now);
  void SetMouth(int32_t resource, bool enabled, uint64_t now);
  Cubism2Model& Model(int32_t resource);
  const Live2dModelAssets& Assets(int32_t resource) const;
  Live2dDriverObservation Observe(int32_t resource) const;
  std::array<float, 16> Matrix(int32_t resource, float width = 960,
                               float height = 544) const;
  // Caller owns decoded/uploaded textures and the OpenGL context. Draw follows
  // VM insertion order and uses the original logical viewport by default.
  void Draw(const Live2dState& state, float width = 960, float height = 544);

 private:
  struct Item;
  Item& Get(int32_t resource);
  const Item& Get(int32_t resource) const;
  Item& Get(const Live2dRecord& record);
  GameResources& resources_;
  Live2dEnvironment environment_;
  Cubism2Sdk sdk_;
  std::map<int32_t, std::unique_ptr<Item>> items_;
  bool gateReady_ = true;
};
}  // namespace Impacto::Saekano
