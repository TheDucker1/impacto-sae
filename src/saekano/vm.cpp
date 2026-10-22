#include "vm.h"
#include "messagekeywords.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace Impacto::Saekano {

CompletedCommand VmBackend::Route(uint16_t, std::span<const uint8_t>) const { return {}; }
void VmBackend::StartPresentation(size_t, uint16_t, std::span<const uint8_t>, bool, uint64_t) {
  throw UnsupportedCommand("presentation backend does not implement this command");
}
bool VmBackend::UpdatePresentation(size_t, bool, UpdateRoute, uint64_t) {
  throw UnsupportedCommand("presentation backend does not implement this command");
}

VirtualMachine::VirtualMachine(ScriptSource& scripts, VmBackend& backend, SemanticState initial,
                               MessageRuntimeContext messageContext)
    : scripts_(scripts), backend_(backend), state_(std::move(initial)),
      messageContext_(messageContext) {
  for (auto cursor : state_.LaneCommandIndex)
    if (cursor != -1) throw UnsupportedCommand("restoring active lane queues requires queue state");
  if (state_.FirstAsyncCommandIndex != -1)
    throw UnsupportedCommand("restoring queued command state is not implemented");
  if (state_.PendingCommandIndex != -1 || state_.PendingFileId != -2)
    throw FormatError("initial state must be before command start");
  Load(false);
}

void VirtualMachine::Load(bool preserveOwnerState) {
  script_ = std::make_unique<ScriptObject>(scripts_.Load(state_.ScriptFileId));
  if (state_.CurrentCommandIndex < 0 ||
      size_t(state_.CurrentCommandIndex) >= script_->Records().size())
    throw FormatError("initial command outside loaded script");
  commands_.clear();
  commands_.resize(script_->Records().size());
  for (size_t i = 0; i < commands_.size(); ++i) {
    const auto opcode = script_->Records()[i].Opcode;
    auto& command = commands_[i];
    if (opcode == 0x64) {
      command.Message = DecodeMessage(script_->Payload(i));
      command.Routing = {command.Message->Route != 0, command.Message->Lane};
    } else if (opcode == 0x438) {
      command.Live2d = DecodeLive2d(script_->Payload(i));
    } else if (opcode == 0x3f4) {
      command.BattleGauge = BattleGaugeCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x3f5) {
      command.ApproachGauge = ApproachGaugeCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x2c0) {
      PayloadReader reader(script_->Payload(i));
      command.WaitDuration = reader.I32();
      command.WaitHonorSkip = reader.U32();
    } else if (opcode == 0x3f2) {
      command.BattleDuration = DecodeChoice(script_->Payload(i), true).DurationMs;
    } else if (opcode == 0x12c || opcode == 0x12d) {
      PayloadReader reader(script_->Payload(i));
      command.WindowDuration = reader.U32();
      command.WindowChannel = reader.U32() == 0 ? 0 : 1;
    } else if (backend_.Supports(opcode)) {
      command.Routing = backend_.Route(opcode, script_->Payload(i));
      if (command.Routing.QueueForLane && command.Routing.Lane >= 5)
        throw FormatError("presentation lane exceeds engine lane count");
    }
  }
  if (preserveOwnerState) {
    // Called after context reset by the outer file-chain path. The owner banks,
    // flags, titles and metadata survive; per-manager presentation does not.
    state_.PendingCommandIndex = -1;
    state_.PendingFileId = -2;
    state_.SkipPhase = 0;
    state_.ExitRequested = 0;
    state_.SpeakerId = -1;
    state_.ActiveText.clear();
    state_.Messages = {};
    state_.LaneCommandIndex.fill(-1);
    state_.FirstAsyncCommandIndex = -1;
    state_.DivergenceStack.clear();
    state_.Characters.clear();
    state_.WindowOpenness = {};
    state_.WindowTypes = {};
    state_.Live2d = {};
    if (auto* models = backend_.Models()) models->Reset();
    state_.ApproachGauge = 0;
    state_.BattleGauge = 50;
    state_.Battle = {};  // Mode entry must initialize its display and sides.
    state_.Approach = {};
    state_.Random = {};  // Native creates a freshly seeded manager. Host supplies its clock seed.
    if (const auto* environment = backend_.ApproachEnvironment()) {
      state_.Approach.Known = true;
      state_.Approach.Particles.resize(environment->ParticleCount);
    }
    state_.DayEnabled = 0;
    const auto keywordSuppressed = messageContext_.KeywordSuppressed;
    messageContext_ = {};
    messageContext_.KeywordSuppressed = keywordSuppressed;  // owner S survives
    dispatcher_ = {};
  }
  messageKeywords_ = {};
  for (size_t side = 0; side < 2; ++side)
    for (const auto& text : state_.Messages[side]) {
      const auto keywords = OriginalMessageKeywords(text);
      messageKeywords_[side].insert(messageKeywords_[side].end(), keywords.begin(), keywords.end());
    }
}

uint16_t VirtualMachine::Opcode() const {
  if (state_.CurrentCommandIndex < 0 || size_t(state_.CurrentCommandIndex) >= script_->Records().size())
    return 0xffff;
  return script_->Records()[size_t(state_.CurrentCommandIndex)].Opcode;
}
VmEvent VirtualMachine::Event(VmBoundary boundary) const {
  return {boundary, state_.ScriptFileId, state_.CurrentCommandIndex, Opcode()};
}
void VirtualMachine::SetMessageContext(MessageRuntimeContext context) {
  if (boundary_ != VmBoundary::BeforeStart && boundary_ != VmBoundary::FileLoadedBeforeStart)
    throw std::runtime_error("message context must be supplied before Start");
  messageContext_ = context;
}
void VirtualMachine::SetGauge(bool battle, int32_t value) {
  if (boundary_ != VmBoundary::BeforeStart && boundary_ != VmBoundary::FileLoadedBeforeStart)
    throw std::runtime_error("gauge intervention must precede Start");
  (battle ? state_.BattleGauge : state_.ApproachGauge) = value;
}
void VirtualMachine::SeedRandom(uint32_t seed) {
  if (boundary_ != VmBoundary::BeforeStart && boundary_ != VmBoundary::FileLoadedBeforeStart)
    throw std::runtime_error("random initialization must precede Start");
  state_.Random.Seed(seed);
}
std::vector<GameEvent> VirtualMachine::TakeEvents() {
  return std::exchange(state_.PendingEvents, {});
}
void VirtualMachine::ActivateLane(size_t lane) {
  if (!dispatcher_.LaneQueues.at(lane).empty() && state_.LaneCommandIndex.at(lane) == -1)
    state_.LaneCommandIndex[lane] = 0;
}

void VirtualMachine::SetupMessage(size_t index, uint64_t now) {
  if (!messageContext_.Known)
    throw UnsupportedCommand("restored message policy/split-side context is missing");
  const auto& message = *commands_[index].Message;
  const size_t context = messageContext_.SplitDialogue && message.SpeakerId != -1 &&
      message.SpeakerId != messageContext_.Side0Character ? 1 : 0;
  const auto keywords = OriginalMessageKeywords(message.Text);
  if ((!keywords.empty() || (messageContext_.PreviousCompletionPolicy != 0 && !messageKeywords_[context].empty())) &&
      !messageContext_.KeywordSuppressed.has_value())
    throw UnsupportedCommand("restored message keyword suppression input is missing");
  if (messageContext_.PreviousCompletionPolicy == 0) {
    state_.Messages[context].clear();
    messageKeywords_[context].clear();
  }
  commands_[index].MessageSide = context;
  messageKeywords_[context].insert(messageKeywords_[context].end(), keywords.begin(), keywords.end());
  state_.Messages[context].push_back(message.Text);
  state_.SpeakerId = message.SpeakerId;
  if (context == 0) {
    state_.ActiveText.clear();
    for (const auto& text : state_.Messages[0]) state_.ActiveText += text;
  }
  backend_.BeginMessage(index, message, context, now);
}
void VirtualMachine::CollectMessageKeywords(size_t index) {
  const auto& keywords = messageKeywords_[commands_[index].MessageSide];
  if (keywords.empty()) return;
  if (!messageContext_.KeywordSuppressed.has_value())
    throw UnsupportedCommand("restored message keyword suppression input is missing");
  if (!*messageContext_.KeywordSuppressed)
    for (const auto keyword : keywords) state_.Bank0.at(size_t(keyword)) = 1;
}

void VirtualMachine::Start(size_t index, uint64_t now, bool lane) {
  auto& command = commands_.at(index);
  const auto opcode = script_->Records()[index].Opcode;
  if (lane) { command.LaneStarted = true; command.LaneEpoch = now; }
  else {
    command.Started = true;
    command.LaneStarted = false;
    command.AdvancePending = false;
    command.Epoch = now;
    command.SelectedOrdinal = 0;
  }
  if (opcode == 0x64) {
    if (lane || !command.Routing.QueueForLane) SetupMessage(index, now);
  } else if (opcode == 0x3f5 && !lane) {
    const auto* environment = backend_.ApproachEnvironment();
    if (!environment) throw UnsupportedCommand("approach gauge environment is unavailable");
    StartApproachGauge(state_, command.ApproachAnimation, *command.ApproachGauge, *environment, now);
  } else if (opcode == 0x3f4 && !lane) {
    const auto* environment = backend_.BattleEnvironment();
    if (!environment) throw UnsupportedCommand("battle gauge environment is unavailable");
    StartBattleGauge(state_, command.BattleAnimation, *command.BattleGauge,
        *environment, backend_.Models(), now);
  } else if (opcode == 0x438 && !lane) {
    if (!state_.Live2d.Known)
      throw UnsupportedCommand("restored Live2D records/parameter slots are missing");
    auto* models = backend_.Models();
    if (!models) throw UnsupportedCommand("Live2D resource/model backend is unavailable");
    StartLive2d(state_.Live2d, *command.Live2d, *models, now);
  } else if (opcode == 0x65 && !lane) {
    command.SavedDay = uint8_t(state_.DayEnabled);
    state_.DayEnabled = 0;
  } else if ((opcode == 0x66 || opcode == 0x1fb || opcode == 0x3ea) && !lane) {
    CompleteCommand(state_, opcode, script_->Payload(index), {});
    if (opcode == 0x66) messageKeywords_[0].clear();
  } else if (opcode == 0x12e && !lane) {
    if (!messageContext_.Known)
      throw UnsupportedCommand("restored split-side context is missing for window type");
    PayloadReader reader(script_->Payload(index));
    const auto speaker = reader.I32();
    const auto type = reader.U32();
    state_.WindowTypes[speaker == messageContext_.Side0Character ? 0 : 1] = type;
  } else if (backend_.Supports(opcode) && (lane || !command.Routing.QueueForLane)) {
    backend_.StartPresentation(index, opcode, script_->Payload(index), lane, now);
  }
}

bool VirtualMachine::Update(size_t index, uint64_t now, const CommandInput& input, bool lane) {
  auto& command = commands_.at(index);
  const auto opcode = script_->Records()[index].Opcode;
  if (!lane && command.Routing.QueueForLane) return true;
  if (lane && !command.LaneStarted) Start(index, now, true);
  if (opcode == 0x3f5) {
    if (lane) return true;
    if (!command.Started)
      throw UnsupportedCommand("approach gauge Start was suppressed; animation state is unavailable");
    const auto* environment = backend_.ApproachEnvironment();
    if (!environment) throw UnsupportedCommand("approach gauge environment is unavailable");
    return UpdateApproachGauge(state_, command.ApproachAnimation, *command.ApproachGauge,
        *environment, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x3f4) {
    if (lane) return true;
    if (!command.Started)
      throw UnsupportedCommand("battle gauge Start was suppressed; animation state is unavailable");
    return UpdateBattleGauge(state_, command.BattleAnimation, backend_.Models(),
        input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x438) {
    if (lane) return true;  // Native shared lane handlers only mark completion.
    if (!command.Started)
      throw UnsupportedCommand("Live2D Start was suppressed; runtime state is unavailable");
    auto* models = backend_.Models();
    if (!models) throw UnsupportedCommand("Live2D resource/model backend is unavailable");
    return UpdateLive2d(state_.Live2d, *command.Live2d, *models,
                         input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x64) {
    if (!lane && !command.Started)
      throw UnsupportedCommand("message Start was suppressed; runtime reveal state is unavailable");
    if (lane && input.Route == UpdateRoute::Skipping) {
      backend_.RevealMessage(index);
      CollectMessageKeywords(index);
      messageContext_.PreviousCompletionPolicy = command.Message->CompletionPolicy;
      return true;
    }
    if (command.AdvancePending) {
      command.AdvancePending = false;
      messageContext_.PreviousCompletionPolicy = command.Message->CompletionPolicy;
      return true;
    }
    if (!backend_.MessageRevealed(index, now)) {
      if (input.Advance || input.Route == UpdateRoute::Skipping) backend_.RevealMessage(index);
      return false;
    }
    CollectMessageKeywords(index);
    if (input.Route == UpdateRoute::Skipping || command.Message->CompletionPolicy == 1 ||
        ((command.Message->CompletionPolicy == 0 || command.Message->CompletionPolicy == 2) &&
         (input.Advance || backend_.MessageAdvanceAllowed(index, now))))
      command.AdvancePending = true;
    return false;
  }
  if (opcode == 0x2c0) {
    if (input.Route == UpdateRoute::Skipping && command.WaitHonorSkip == 0) return true;
    if (!command.Started)
      throw UnsupportedCommand("wait Start was suppressed; native epoch is unavailable");
    if (input.Route == UpdateRoute::Skipping) state_.SkipPhase = 2;
    const uint64_t deadline = command.Epoch + uint64_t(int64_t(command.WaitDuration));
    const bool complete = now >= deadline;
    if (complete && input.Route == UpdateRoute::Skipping) state_.SkipPhase = 1;
    return complete;
  }
  if ((opcode == 0x12c || opcode == 0x12d) && !lane) {
    const auto elapsed = now - command.Epoch;
    const auto progress = input.Route == UpdateRoute::Skipping || command.WindowDuration == 0 ||
        elapsed >= command.WindowDuration ? 100u : uint32_t(elapsed * 100 / command.WindowDuration);
    state_.WindowOpenness[command.WindowChannel] = opcode == 0x12c ? progress : 100 - progress;
    return progress == 100;
  }
  if (lane) {
    if (backend_.Supports(opcode))
      return backend_.UpdatePresentation(index, true, input.Route, now);
    // Shared base lane handlers mark only lane completion; they do not repeat
    // variable/control-flow effects from the main execution route.
    return true;
  }
  if (backend_.Supports(opcode)) return backend_.UpdatePresentation(index, false, input.Route, now);
  if (opcode == 0x12e) return true;
  auto commitInput = input;
  if (opcode == 0x65 || opcode == 0x3f6) {
    if (!input.Choice) return false;
  } else if (opcode == 0x3f2) {
    if (input.Choice) command.SelectedOrdinal = *input.Choice;
    const bool timeout = input.BattleTimeout || now >= command.Epoch + command.BattleDuration;
    if (!input.Choice && !timeout) return false;
    commitInput.Choice = command.SelectedOrdinal;
    commitInput.BattleTimeout = timeout;
  }
  CompleteCommand(state_, opcode, script_->Payload(index), commitInput);
  if (opcode == 0x65) state_.DayEnabled = int32_t(command.SavedDay);
  return true;
}

void VirtualMachine::UpdateLanes(uint64_t now) {
  ProcessLanes(state_, dispatcher_, [&](size_t index, UpdateRoute route) {
    CommandInput input; input.Route = route;
    return Update(index, now, input, true);
  });
}

VmEvent VirtualMachine::Step(uint64_t now, const CommandInput& input) {
  try {
    if (boundary_ == VmBoundary::Ended) return Event(boundary_);
    if (boundary_ == VmBoundary::CommandCompletedBeforeDispatch) {
      const auto index = size_t(state_.CurrentCommandIndex);
      const auto result = DispatchCompleted(state_, dispatcher_, script_->Records().size(), commands_[index].Routing);
      if (result == DispatchResult::FileRequested) {
        if (state_.PendingFileId < 0) boundary_ = VmBoundary::Ended;
        else {
          state_.ScriptFileId = state_.PendingFileId;
          state_.CurrentCommandIndex = 0;
          Load(true);
          boundary_ = VmBoundary::FileLoadedBeforeStart;
        }
      } else if (result == DispatchResult::ScriptEnded) boundary_ = VmBoundary::Ended;
      else if (result == DispatchResult::DeferredStart) {
        boundary_ = VmBoundary::WaitingBackend;
        UpdateLanes(now);
      } else boundary_ = VmBoundary::BeforeStart;
      return Event(boundary_);
    }
    const auto index = size_t(state_.CurrentCommandIndex);
    if (boundary_ == VmBoundary::BeforeStart || boundary_ == VmBoundary::FileLoadedBeforeStart) {
      Start(index, now, false);
      boundary_ = VmBoundary::CommandStarted;
      if (Opcode() == 0x64) UpdateLanes(now);
      return Event(boundary_);
    }
    if (Update(index, now, input, false)) boundary_ = VmBoundary::CommandCompletedBeforeDispatch;
    else {
      boundary_ = (Opcode() == 0x65 || Opcode() == 0x3f2 || Opcode() == 0x3f6)
          ? VmBoundary::WaitingInput : VmBoundary::WaitingBackend;
      UpdateLanes(now);
    }
    return Event(boundary_);
  } catch (const std::exception& error) {
    throw std::runtime_error("script " + std::to_string(state_.ScriptFileId) +
        " command " + std::to_string(state_.CurrentCommandIndex) + " opcode " +
        std::to_string(Opcode()) + ": " + error.what());
  }
}

}  // namespace Impacto::Saekano
