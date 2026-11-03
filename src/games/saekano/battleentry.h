#pragma once
#include "approachentry.h"
#include <array>

namespace Impacto::Saekano {
struct SemanticState;
struct BattleEntryCommand {
  int32_t History=0;
  std::array<int32_t,2> Groups{},Entries{};
  int32_t Gauge=50;
  static BattleEntryCommand Decode(std::span<const uint8_t> payload);
};
struct BattleEntryState {
  bool Known=false,Enabled=false;
  uint8_t MessageModeByte=0; // Native C1c25, retained neutrally until its consumers are bound.
  struct Policy { uint8_t MessageModeByte=0;int32_t Phase=0,Mode=0; };
  std::optional<Policy> SavedPolicy;
  int32_t SelectLoadedCount=1,SelectStartedCount=0;
  std::optional<BattleEntryCommand> Request;
};
class BattleResources {
 public:
  virtual ~BattleResources()=default;
  virtual void Begin(const BattleEntryCommand& command)=0;
  virtual void Pump()=0;
  virtual ApproachResourceStatus Status() const=0;
  virtual void Reset()=0;
};
void InitializeFreshBattleOwner(SemanticState& state);
void LoadBattleEntryFields(SemanticState& state,const BattleEntryCommand& command);
void StartBattleEntry(SemanticState& state,const BattleEntryCommand& command,BattleResources& resources);
bool UpdateBattleEntry(SemanticState& state,const BattleResources& resources,bool skipping);
}
