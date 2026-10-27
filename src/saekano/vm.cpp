#include "vm.h"
#include "messagekeywords.h"
#include "skipauto.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace Impacto::Saekano {
bool VirtualMachine::SkipHistoryGate(const HistoryEnvironment& environment,
                                     const ScriptReadHistory* currentFileHistory) const {
  if (state_.CurrentCommandIndex < 0)
    throw UnsupportedCommand("history query has no current command");
  return SkipHistoryEligible(environment, script_->Records(),
                              uint32_t(state_.CurrentCommandIndex), currentFileHistory);
}
void VirtualMachine::UpdateSkipAuto(SkipAutoState& policy,
                                   const SkipAutoInput& input,
                                   int32_t autoKeyDuration, uint64_t now) {
  if (profile_) throw UnsupportedCommand("profile owns the active input loop");
  UpdateSkipAutoPolicy(state_, policy, input, autoKeyDuration, now);
}

CompletedCommand VmBackend::Route(uint16_t, std::span<const uint8_t>) const { return {}; }
const ChapterTitleAnimation* VirtualMachine::CurrentChapter() const {
  if (Opcode() != 0x3ec) return nullptr;
  return &commands_.at(size_t(state_.CurrentCommandIndex)).ChapterAnimation;
}
void VmBackend::StartPresentation(size_t, uint16_t, std::span<const uint8_t>, bool, uint64_t) {
  throw UnsupportedCommand("presentation backend does not implement this command");
}
bool VmBackend::UpdatePresentation(size_t, bool, UpdateRoute, uint64_t) {
  throw UnsupportedCommand("presentation backend does not implement this command");
}

VirtualMachine::VirtualMachine(ScriptSource& scripts, VmBackend& backend,
                               SemanticState initial,
                               MessageRuntimeContext messageContext)
    : VirtualMachine(scripts, backend, std::move(initial), messageContext,
                     false) {}
VirtualMachine::VirtualMachine(ScriptSource& scripts, VmBackend& backend,
                               SemanticState initial,
                               MessageRuntimeContext messageContext,
                               bool replaceManager)
    : scripts_(scripts),
      backend_(backend),
      state_(std::move(initial)),
      messageContext_(messageContext) {
  for (auto cursor : state_.LaneCommandIndex)
    if (cursor != -1) throw UnsupportedCommand("restoring active lane queues requires queue state");
  if (state_.FirstAsyncCommandIndex != -1)
    throw UnsupportedCommand("restoring queued command state is not implemented");
  if (state_.PendingCommandIndex != -1 || state_.PendingFileId != -2)
    throw FormatError("initial state must be before command start");
  Load(replaceManager);
}
std::unique_ptr<VirtualMachine> VirtualMachine::FromCompletedProfile(
    ScriptSource& scripts, VmBackend& backend, SemanticState owner,
    const ProfileMode& completed, MessageRuntimeContext messageContext) {
  if (completed.CurrentStage() != ProfileMode::Stage::Complete ||
      completed.TargetScript() < 0)
    throw UnsupportedCommand("profile has no completed script destination");
  owner.ScriptFileId = completed.TargetScript();
  owner.CurrentCommandIndex = 0;
  // Suspended manager fields belong to the destroyed manager, never its owner.
  owner.PendingCommandIndex = -1;
  owner.PendingFileId = -2;
  owner.LaneCommandIndex.fill(-1);
  owner.FirstAsyncCommandIndex = -1;
  auto vm = std::unique_ptr<VirtualMachine>(new VirtualMachine(
      scripts, backend, std::move(owner), messageContext, true));
  vm->boundary_ = VmBoundary::ProfileDestinationLoaded;
  return vm;
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
    } else if (opcode == 0x3ec) {
      command.Chapter = ChapterTitleCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x258) {
      command.PlayBgm = PlayBgmCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x25a) {
      command.PlaySe = PlaySeCommand::Decode(script_->Payload(i));
      command.Routing = {command.PlaySe->Route != 0, command.PlaySe->Lane};
    } else if (opcode == 0x25b) {
      command.StopSe = StopSeCommand::Decode(script_->Payload(i));
    } else if (opcode == 0xc8 || opcode == 0xc9) {
      command.ScreenFade = ScreenFadeCommand::Decode(script_->Payload(i), opcode == 0xc9);
    } else if (opcode == 0x3fd || opcode == 0x400) {
      command.ScreenFilter = ScreenFilterCommand::Decode(script_->Payload(i), opcode == 0x400);
    } else if (opcode == 0x191) {
      command.RuleWipe = RuleWipeCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x190) {
      command.Quake = QuakeCommand::Decode(script_->Payload(i));
    } else if (opcode == 0xcb) {
      command.DrawImage = DrawImageCommand::Decode(script_->Payload(i));
      command.Routing = {command.DrawImage->Route != 0, command.DrawImage->Lane};
    } else if (opcode == 0xca) {
      command.FillRect = FillRectCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x201) {
      command.CharacterColor = CharacterColorCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x1f5) {
      command.CharacterPosition = CharacterPositionCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterPosition->Route != 0,command.CharacterPosition->Lane};
    } else if (opcode == 0x1f7) {
      command.CharacterOffset = CharacterOffsetCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterOffset->Route != 0,command.CharacterOffset->Lane};
    } else if (opcode == 0x1f6) {
      command.CharacterMove = CharacterMoveCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterMove->Route != 0,command.CharacterMove->Lane};
    } else if (opcode == 0x1f4) {
      command.CharacterDraw = CharacterDrawCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterDraw->Route != 0,command.CharacterDraw->Lane};
    } else if (opcode == 0x1f9) {
      command.CharacterPose = CharacterPoseCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterPose->Route != 0,command.CharacterPose->Lane};
    } else if (opcode == 0x1fa) {
      command.CharacterFace = CharacterFaceCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterFace->Route != 0,command.CharacterFace->Lane};
    } else if (opcode == 0x204) {
      command.CharacterReserve = CharacterReserveCommand::Decode(script_->Payload(i));
    } else if (opcode == 0x1f8) {
      command.CharacterScale = CharacterScaleCommand::Decode(script_->Payload(i));
      command.Routing = {command.CharacterScale->Route != 0,command.CharacterScale->Lane};
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
    spriteBlinks_.clear();
    freshSpriteFrames_ = true;
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
    // BindScenarioOwner80071c3c..c62 clears these owner bytes even though
    // ordinary owner variables/history survive manager replacement.
    state_.ApproachFlags.fill(0);
    state_.DivergenceStack.clear();
    state_.Characters.clear();
    state_.CharacterRuntimeState = {true,0xffffffff,{},{},0};
    state_.PoseCache = FreshCharacterPoseCache();
    if (auto* poses = backend_.Poses()) poses->Reset();
    state_.WindowOpenness = {};
    state_.WindowTypes = {};
    state_.Live2d = {};
    if (auto* models = backend_.Models()) models->Reset();
    state_.ApproachGauge = 0;
    state_.BattleGauge = 50;
    state_.Battle = {};  // Mode entry must initialize its display and sides.
    state_.BgmTrack.reset();
    state_.BgmCommandVolume.reset();
    // Reset801a015c/0164/016c: remembered SE ID=-1, volume100, channel0.
    state_.SeTrack = -1;
    state_.SeCommandVolume = 100;
    state_.SeChannel = 0;
    state_.Effects = {};
    state_.RuleWipe = {};
    // Fresh constructor801a0940..0954 clears all six quake outputs.
    for (auto& pair : state_.Quake) pair = std::array<int32_t,2>{};
    if (auto* driver = backend_.Images()) {
      state_.SceneImages = FreshSceneImages(*driver);
    } else if (state_.SceneImages.Known) {
      throw UnsupportedCommand("scene image resource pool is unavailable");
    } else {
      state_.SceneImages.FillRectsKnown.fill(false);
    }
    if (auto* images = backend_.Images()) {
      images->Release(ImageSlot::Rule);
      images->Release(ImageSlot::Background);
    }
    // Fresh manager constructor801a04a4: C1600=7, C160c/C1610=0,
    // inactive rule flags and both cache IDs=-1. Restored initial state differs.
    state_.Effects = {true, 7, 0, 0};
    state_.RuleWipe.Known = true;
    state_.ScreenFade = 255;
    state_.ScreenFilter = FreshScreenFilter();
    state_.TitleDisplay = false;
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
const QuakeAnimation* VirtualMachine::CurrentQuake() const {
  if (Opcode() != 0x190) return nullptr;
  return &commands_[size_t(state_.CurrentCommandIndex)].QuakeRuntime;
}
VmEvent VirtualMachine::Event(VmBoundary boundary) const {
  return {boundary, state_.ScriptFileId, state_.CurrentCommandIndex, Opcode()};
}
VmEvent VirtualMachine::FrameEvent(VmBoundary boundary, uint64_t now) {
  if (auto* poses = backend_.Poses()) PollCharacterPoseLoads(state_.PoseCache, *poses);
  // Native800740c4 schedules shared character queues after main/lane handlers.
  // Queue completion therefore becomes visible to a waiting command next step.
  TickCharacterQueues(state_, now);
  return Event(boundary);
}
void VirtualMachine::SetMessageContext(MessageRuntimeContext context) {
  if (boundary_ != VmBoundary::BeforeStart && boundary_ != VmBoundary::FileLoadedBeforeStart &&
      boundary_ != VmBoundary::ProfileDestinationLoaded)
    throw std::runtime_error("message context must be supplied before Start");
  messageContext_ = context;
}
void VirtualMachine::SetGauge(bool battle, int32_t value) {
  if (boundary_ != VmBoundary::BeforeStart && boundary_ != VmBoundary::FileLoadedBeforeStart &&
      boundary_ != VmBoundary::ProfileDestinationLoaded)
    throw std::runtime_error("gauge intervention must precede Start");
  (battle ? state_.BattleGauge : state_.ApproachGauge) = value;
}
void VirtualMachine::SeedRandom(uint32_t seed) {
  if (boundary_ != VmBoundary::BeforeStart && boundary_ != VmBoundary::FileLoadedBeforeStart &&
      boundary_ != VmBoundary::ProfileDestinationLoaded)
    throw std::runtime_error("random initialization must precede Start");
  state_.Random.Seed(seed);
}
void VirtualMachine::UpdateSpriteFrames(uint32_t clockSeed) {
  if (!state_.Random.Known())
    throw UnsupportedCommand("sprite frame RNG state is missing");
  for (const auto& [id, actor] : state_.Characters)
    if (!actor.RuntimeKnown || !actor.Pose.Known ||
        (!freshSpriteFrames_ && !spriteBlinks_.contains(id)))
      throw UnsupportedCommand("restored sprite frame schedule is missing");
  for (const auto& [id, actor] : state_.Characters) {
    auto [found, inserted] = spriteBlinks_.try_emplace(id);
    if (inserted) found->second.Initialize(clockSeed);
    found->second.Update(state_.Random, actor.Pose.Ready,
                         actor.Tweens[size_t(CharacterTween::Opacity)].Current);
  }
}
const CharacterBlink* VirtualMachine::SpriteBlink(uint16_t id) const {
  const auto found = spriteBlinks_.find(id);
  return found == spriteBlinks_.end() ? nullptr : &found->second;
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
  } else if (opcode == 0x3ed && !lane) {
    PayloadReader reader(script_->Payload(index));
    state_.OtakuMode = reader.I32();
    // Native decoder8007de52 consumes only this word; original records also
    // carry six trailing bytes that are not decoded as profile fields.
    state_.PendingEvents.push_back({GameEventKind::PauseChannel, 0, {}, 1});
    state_.PendingEvents.push_back({GameEventKind::PumpAudio, 0});
    state_.PendingEvents.push_back({GameEventKind::StopBgm, 0});
    state_.PendingEvents.push_back({GameEventKind::StopSe, 2});
    state_.PendingEvents.push_back({GameEventKind::StopSe, 3});
  } else if (opcode == 0x3ec && !lane) {
    const auto* environment = backend_.ChapterConfig();
    if (!environment) throw UnsupportedCommand("chapter title environment is unavailable");
    StartChapterTitle(state_, command.ChapterAnimation, *command.Chapter, *environment, now);
  } else if ((opcode == 0xc8 || opcode == 0xc9) && !lane) {
    StartScreenFade(state_, command.FadeAnimation, *command.ScreenFade, now);
  } else if ((opcode == 0x3fd || opcode == 0x400) && !lane) {
    StartScreenFilter(state_.ScreenFilter, command.FilterAnimation, *command.ScreenFilter, now);
  } else if (opcode == 0xcb) {
    auto* images = backend_.Images();
    if (!images) throw UnsupportedCommand("scene image resource backend is unavailable");
    StartDrawImage(state_, command.ImageAnimation, *command.DrawImage, *images, now);
  } else if (opcode == 0xca && !lane) {
    StartFillRect(state_, command.FillAnimation, *command.FillRect, now);
  } else if (opcode == 0xce && !lane) {
    auto* images = backend_.Images();
    if (!images) throw UnsupportedCommand("scene image resource backend is unavailable");
    StartReleaseImage(state_, ReleaseImageCommand::Decode(script_->Payload(index)), *images);
  } else if (opcode == 0x201 && !lane) {
    StartCharacterColor(state_, *command.CharacterColor, now);
  } else if (opcode == 0x1f5 && !lane) {
    StartCharacterPosition(state_, *command.CharacterPosition);
  } else if (opcode == 0x1f7 && !lane) {
    StartCharacterOffset(state_, command.OffsetAnimation, *command.CharacterOffset);
  } else if (opcode == 0x1f6 && !lane) {
    StartCharacterMove(state_, command.MoveAnimation, *command.CharacterMove);
  } else if (opcode == 0x1f4 && !lane) {
    StartCharacterDraw(state_, command.DrawAnimation, *command.CharacterDraw, now);
  } else if (opcode == 0x1f9 && !lane) {
    auto* poses = backend_.Poses();
    if (!poses) throw UnsupportedCommand("character pose resource backend is unavailable");
    StartCharacterPose(state_, command.PoseAnimation, *command.CharacterPose, *poses);
  } else if (opcode == 0x1fa && !lane) {
    StartCharacterFace(state_, command.FaceAnimation, *command.CharacterFace);
  } else if (opcode == 0x204 && !lane) {
    StartCharacterReserve(state_, *command.CharacterReserve);
  } else if (opcode == 0x1f8 && !lane) {
    StartCharacterScale(state_, command.ScaleAnimation, *command.CharacterScale);
  } else if (opcode == 0x191 && !lane) {
    auto* images = backend_.Images();
    if (!images) throw UnsupportedCommand("scene image resource backend is unavailable");
    StartRuleWipe(state_, command.WipeAnimation, *command.RuleWipe, *images, now);
  } else if (opcode == 0x190 && !lane) {
    StartQuake(state_, command.QuakeRuntime, *command.Quake, now);
  } else if (opcode == 0x258 && !lane) {
    auto* music = backend_.Music();
    if (!music) throw UnsupportedCommand("original BGM resource backend is unavailable");
    StartPlayBgm(state_, *command.PlayBgm, *music);
  } else if (opcode == 0x25a && (lane || !command.Routing.QueueForLane)) {
    auto* sounds = backend_.SoundEffects();
    if (!sounds) throw UnsupportedCommand("original SE resource backend is unavailable");
    StartPlaySe(state_, *command.PlaySe, *sounds);
  } else if (opcode == 0x259 && !lane) {
    PayloadReader reader(script_->Payload(index));
    const auto fade = reader.I32();
    state_.BgmTrack = -1;
    state_.PendingEvents.push_back({GameEventKind::StopBgm, 0, {}, fade});
  } else if (opcode == 0x25b && !lane) {
    StartStopSe(state_, *command.StopSe);
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
  if (!lane && command.Routing.QueueForLane && opcode != 0xcb && opcode != 0x1f9) return true;
  if (lane && !command.LaneStarted) Start(index, now, true);
  if (opcode == 0x258 || opcode == 0x259 || opcode == 0x25a || opcode == 0x25b || opcode == 0xce || opcode == 0x204) return true;  // Native common completion; no fade/resource wait.
  if (opcode == 0x201) {
    if (lane) return true;
    return UpdateCharacterColor(state_, *command.CharacterColor, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x1f5) {
    UpdateCharacterPosition(state_, *command.CharacterPosition, lane, now);
    return true;  // Normal/skip have identical effects; lanes complete independently.
  }
  if (opcode == 0x1f7) {
    return UpdateCharacterOffset(state_, command.OffsetAnimation, *command.CharacterOffset,
                                 lane, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x1f6) {
    return UpdateCharacterMove(state_, command.MoveAnimation, *command.CharacterMove,
        lane, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x1f4) {
    return UpdateCharacterDraw(state_, command.DrawAnimation, *command.CharacterDraw,
        lane, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x1f9) {
    auto* poses = backend_.Poses();
    if (!poses) throw UnsupportedCommand("character pose resource backend is unavailable");
    bool ready = poses->GateReady();
    if (auto* models = backend_.Models()) ready &= models->GateReady();
    if (auto* images = backend_.Images()) ready &= images->Ready();
    return UpdateCharacterPose(state_, command.PoseAnimation, *command.CharacterPose,
        *poses, ready, lane, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x1fa) {
    return UpdateCharacterFace(state_, command.FaceAnimation, *command.CharacterFace,
        lane, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x1f8) {
    return UpdateCharacterScale(state_, command.ScaleAnimation, *command.CharacterScale,
        lane, input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0xcb) {
    auto* images = backend_.Images();
    if (!images) throw UnsupportedCommand("scene image resource backend is unavailable");
    return UpdateDrawImage(state_, command.ImageAnimation, *command.DrawImage, *images,
        input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0xca) {
    if (lane) return true;
    return UpdateFillRect(state_, command.FillAnimation, *command.FillRect,
                          input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0xc8 || opcode == 0xc9) {
    if (lane) return true;
    return UpdateScreenFade(state_, command.FadeAnimation, *command.ScreenFade,
        input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x3fd || opcode == 0x400) {
    if (lane) return true;
    return UpdateScreenFilter(state_.ScreenFilter, command.FilterAnimation, *command.ScreenFilter,
                               input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x3ec) {
    if (lane) return true;
    const auto* environment = backend_.ChapterConfig();
    if (!environment) throw UnsupportedCommand("chapter title environment is unavailable");
    return UpdateChapterTitle(state_, command.ChapterAnimation, *environment, input.Advance, now);
  }
  if (opcode == 0x191) {
    if (lane) return true;
    auto* images = backend_.Images();
    if (!images) throw UnsupportedCommand("scene image resource backend is unavailable");
    return UpdateRuleWipe(state_, command.WipeAnimation, *command.RuleWipe, *images,
        input.Route == UpdateRoute::Skipping, now);
  }
  if (opcode == 0x190) {
    if (lane) return true;
    return UpdateQuake(state_, command.QuakeRuntime, *command.Quake,
                       input.Route == UpdateRoute::Skipping, now);
  }
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
      backend_.CompleteMessage(index, now);
      command.AdvancePending = false;
      messageContext_.PreviousCompletionPolicy = command.Message->CompletionPolicy;
      // Main normal84964/skip857d6 reset C1490 only on actual completion;
      // async-main completion bypasses this substantive message route.
      if (!lane) state_.FirstAsyncCommandIndex = -1;
      return true;
    }
    if (!backend_.BeginMessageUpdate(index, now, input.Route)) return false;
    if (!backend_.MessageRevealed(index, now)) {
      if (input.Advance || input.Route == UpdateRoute::Skipping) backend_.RevealMessage(index);
      if (input.Route == UpdateRoute::Normal) backend_.EndMessageUpdate(index, now);
      return false;
    }
    CollectMessageKeywords(index);
    if (input.Route == UpdateRoute::Skipping || command.Message->CompletionPolicy == 1 ||
        ((command.Message->CompletionPolicy == 0 || command.Message->CompletionPolicy == 2) &&
         (input.Advance || backend_.MessageAdvanceAllowed(index, now))))
      command.AdvancePending = true;
    if (input.Route == UpdateRoute::Normal) backend_.EndMessageUpdate(index, now);
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
    if (boundary_ == VmBoundary::Ended) return FrameEvent(boundary_, now);
    // Profile owns the outer loop, not a completed script command. Native
    // outer80030ba2 replaces the manager without dispatching3ed's successor.
    if (profile_) {
      if (!profile_->Update(state_, input.Profile, now)) {
        boundary_ = VmBoundary::WaitingInput;
        return Event(boundary_);  // Main manager/lane queues are suspended.
      }
      const int32_t destination = profile_->TargetScript();
      profile_.reset();
      if (destination < 0) boundary_ = VmBoundary::Ended;
      else {
        state_.ScriptFileId = destination;
        state_.CurrentCommandIndex = 0;
        Load(true);
        boundary_ = VmBoundary::ProfileDestinationLoaded;
      }
      return Event(boundary_);
    }
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
      return FrameEvent(boundary_, now);
    }
    const auto index = size_t(state_.CurrentCommandIndex);
    if (boundary_ == VmBoundary::BeforeStart || boundary_ == VmBoundary::FileLoadedBeforeStart ||
        boundary_ == VmBoundary::ProfileDestinationLoaded) {
      Start(index, now, false);
      boundary_ = VmBoundary::CommandStarted;
      if (Opcode() == 0x64) UpdateLanes(now);
      return FrameEvent(boundary_, now);
    }
    if (Opcode() == 0x3ed) {
      if (!commands_[index].Started)
        throw UnsupportedCommand("profile command Start was suppressed");
      auto* services = backend_.ProfileHost();
      const auto* environment = backend_.ProfileConfig();
      const auto* shared = backend_.ModeResultConfig();
      if (!services || !environment || !shared)
        throw UnsupportedCommand("owning profile resources and environment are unavailable");
      // Command Update requests outer entry; it does not run the new mode in
      // that same manager update or mark the command complete.
      profile_ = std::make_unique<ProfileMode>(state_, *services, *environment, *shared, now);
      boundary_ = VmBoundary::WaitingInput;
      return Event(boundary_);
    }
    if (Update(index, now, input, false)) boundary_ = VmBoundary::CommandCompletedBeforeDispatch;
    else {
      boundary_ = (Opcode() == 0x65 || Opcode() == 0x3f2 || Opcode() == 0x3f6)
          ? VmBoundary::WaitingInput : VmBoundary::WaitingBackend;
      UpdateLanes(now);
    }
    return FrameEvent(boundary_, now);
  } catch (const std::exception& error) {
    throw std::runtime_error("script " + std::to_string(state_.ScriptFileId) +
        " command " + std::to_string(state_.CurrentCommandIndex) + " opcode " +
        std::to_string(Opcode()) + ": " + error.what());
  }
}

}  // namespace Impacto::Saekano
