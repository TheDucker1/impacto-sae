#pragma once

#include "flow.h"

#include <memory>

namespace Impacto::Saekano {

struct MessageRuntimeContext {
  // These are native context fields that the original portable schema omitted.
  // Restored callers must supply them before executing text; fresh contexts use
  // constructor defaults. Unknown restored state must not be guessed from text.
  bool Known = true;
  uint32_t PreviousCompletionPolicy = 0;  // C+1598
  bool SplitDialogue = false;            // C+20ac
  int32_t Side0Character = 0;             // C+20b4
};

class ScriptSource {
 public:
  virtual ~ScriptSource() = default;
  virtual Bytes Load(int32_t scriptId) = 0;
};

// Rendering/audio/game-mode gates are an explicit boundary. A backend accepts
// only opcodes it implements. Unknown presentation is never silently completed.
class VmBackend {
 public:
  virtual ~VmBackend() = default;
  virtual void BeginMessage(size_t command, const MessageCommand& message,
                             size_t context, uint64_t now) = 0;
  virtual bool MessageRevealed(size_t command, uint64_t now) = 0;
  virtual bool MessageAdvanceAllowed(size_t command, uint64_t now) = 0;
  virtual void RevealMessage(size_t command) = 0;
  virtual bool Supports(uint16_t) const { return false; }
  virtual CompletedCommand Route(uint16_t opcode, std::span<const uint8_t> payload) const;
  virtual void StartPresentation(size_t command, uint16_t opcode,
                                  std::span<const uint8_t> payload, bool lane, uint64_t now);
  virtual bool UpdatePresentation(size_t command, bool lane, UpdateRoute route, uint64_t now);
};

enum class VmBoundary {
  BeforeStart, CommandStarted, WaitingInput, WaitingBackend,
  CommandCompletedBeforeDispatch, FileLoadedBeforeStart, Ended
};
struct VmEvent {
  VmBoundary Boundary;
  int32_t Script;
  int32_t Command;
  uint16_t Opcode;
};

// Owns decoded script bytes and one mutable runtime object per authored command.
// Each Step exposes a real lifecycle boundary, allowing observers to stop before
// dispatch without reassigning the PC. Callers provide clock and input events.
class VirtualMachine {
 public:
  VirtualMachine(ScriptSource& scripts, VmBackend& backend, SemanticState initial,
                  MessageRuntimeContext messageContext = {});
  const SemanticState& State() const { return state_; }
  const DispatchState& Dispatcher() const { return dispatcher_; }
  VmBoundary Boundary() const { return boundary_; }
  uint16_t Opcode() const;
  VmEvent Step(uint64_t now, const CommandInput& input = {});
  void SetGauge(bool battle, int32_t value);
  void ActivateLane(size_t lane);
  void RequestLaneFlush() { dispatcher_.FlushLanes = true; }
  void SetMessageContext(MessageRuntimeContext context);

 private:
  struct RuntimeCommand {
    bool Started = false;
    bool LaneStarted = false;
    bool AdvancePending = false;
    uint64_t Epoch = 0;
    uint64_t LaneEpoch = 0;
    uint32_t SavedDay = 0;
    CompletedCommand Routing;
    std::optional<MessageCommand> Message;
    int32_t WaitDuration = 0;
    uint32_t WaitHonorSkip = 0;
    size_t SelectedOrdinal = 0;
    uint32_t BattleDuration = 0;
    uint32_t WindowDuration = 0;
    uint32_t WindowChannel = 0;
  };
  ScriptSource& scripts_;
  VmBackend& backend_;
  SemanticState state_;
  MessageRuntimeContext messageContext_;
  DispatchState dispatcher_;
  std::unique_ptr<ScriptObject> script_;
  std::vector<RuntimeCommand> commands_;
  VmBoundary boundary_ = VmBoundary::BeforeStart;
  void Load(bool preserveOwnerState);
  void Start(size_t index, uint64_t now, bool lane);
  bool Update(size_t index, uint64_t now, const CommandInput& input, bool lane);
  void SetupMessage(size_t index, uint64_t now);
  void UpdateLanes(uint64_t now);
  VmEvent Event(VmBoundary boundary) const;
};

}  // namespace Impacto::Saekano
