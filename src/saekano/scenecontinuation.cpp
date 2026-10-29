#include "scenecontinuation.h"

#include "mouthselection.h"
#include "ordinaryvoiceoutput.h"
#include "profilearchive.h"
#include "scenemessage.h"
#include "skipauto.h"
#include "album.h"
#include "missionvoice.h"
#include "approachresulttext.h"
#ifdef SAEKANO_SCENE_CUBISM2
#include "live2ddriver.h"
#endif

#include <fstream>
#include <algorithm>
#include <bit>
#include <iterator>
#include <ostream>
#include <set>
#include <tuple>

namespace Impacto::Saekano {
namespace {
class DiscoveryBuffer final : public std::streambuf {
 public:
  std::streambuf* Target;
  bool Enabled;
  DiscoveryBuffer(std::streambuf* target,bool enabled):Target(target),Enabled(enabled){}
 protected:
  std::streamsize xsputn(const char* data,std::streamsize size) override {
    return Enabled?Target->sputn(data,size):size;
  }
  int_type overflow(int_type value) override {
    if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
    return Enabled?Target->sputc(traits_type::to_char_type(value)):value;
  }
};
Bytes Read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw FormatError("cannot open scene continuation input");
  Bytes bytes{std::istreambuf_iterator<char>(stream), {}};
  if (stream.bad()) throw FormatError("cannot read scene continuation input");
  return bytes;
}
void String(std::ostream& out, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  out << '"';
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << char(c);
    else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
    else out << char(c);
  }
  out << '"';
}
}
int RunSceneContinuation(const std::filesystem::path& root, size_t frameBudget,
                         std::optional<size_t> untilMessage, std::ostream& report,bool confirmFirstMission,bool chooseFirstApproach,
                         std::span<const std::array<size_t,2>> approachPlan) {
  const std::array<std::array<size_t,2>,1> firstChoice{{{0,0}}};
  if(chooseFirstApproach && approachPlan.empty())approachPlan=firstChoice;
  chooseFirstApproach=!approachPlan.empty();
  DiscoveryBuffer output(report.rdbuf(),!confirmFirstMission && !chooseFirstApproach);std::ostream out(&output);
  if (!frameBudget || frameBudget > 1000000)
    throw FormatError("scene continuation frame budget outside1..1000000");
  GameResources resources(Read(root / "resource.dat"));
  const auto profileRaw = Read(root / "env/env_otaku_mode.dat");
  const auto profile = ProfileEnvironment::Decode(profileRaw);
  const auto shared = ModeResultEnvironment::DecodeApproach(Read(root / "env/env_approach.dat"));
  const auto format = TextFormatEnvironment::Decode(Read(root / "env/env_string_format.dat"), true);
  const auto timing = TextTimingEnvironment::Decode(Read(root / "env/env_config.dat"), true, true);
  ArchiveProfileServices services(resources, ScriptNameIndex::FromFirstArchive(Read(root / "first.dat")), format, timing);
  SemanticState owner;
  InitializeFreshAudioOwner(owner);
  InitializeFreshMessageOwner(owner);
  InitializeFreshCgHistory(owner, LoadAlbumConfiguration(Read(root / "first.dat"),
      Read(root / "env/env_omake.dat")).RequiredImages);
  InitializeFreshProfileEntry(owner, ProfileInitialStats::Decode(profileRaw),
                              LoadProfileItemCatalog(Read(root / "first.dat")));
  ProfileMode mode(owner, services, profile, shared);
  ProfileSelectionInput selection;
  selection.Accept = true;
  selection.Confirmation = true;
  uint64_t profileNow = 0;
  for (size_t n = 0; n < 2500 && mode.CurrentStage() != ProfileMode::Stage::Complete; ++n) {
    mode.Update(owner, selection, profileNow += 16);
    owner.PendingEvents.clear();
  }
  if (mode.CurrentStage() != ProfileMode::Stage::Complete)
    throw FormatError("scene continuation profile entry did not complete");
  ArchiveSceneImages images(resources);
  ArchiveApproachResources approachAssets(resources);
  ClockedMissionVoice missionVoice(root);
#ifdef SAEKANO_SCENE_CUBISM2
  SdkLive2dDriver models(resources, Live2dEnvironment::Decode(Read(root / "env/env_live2d.dat")));
#endif
  ArchiveCharacterPoses poses(resources);
  ArchiveBgmResources music(root);
  ArchiveSeResources sounds(root);
  SceneMessageBackend backend(format, timing, *owner.TextSpeed, owner.AutoSpeed);
  backend.AutoReveal = backend.AutoAdvance = false;
  backend.SceneImages = &images; backend.CharacterPoses = &poses;
  backend.ApproachResourceBank = &approachAssets;
  backend.MissionVoiceBank=&missionVoice;
  backend.MissionSettings=ApproachMissionEnvironment::Decode(Read(root/"env/env_approach.dat"));
  backend.SelectionSettings=ApproachSelectionEnvironment::Decode(Read(root/"env/env_approach.dat"));
  backend.ApproachConfig = ApproachGaugeEnvironment::Decode(Read(root / "env/env_approach.dat"));
  backend.MusicResources = &music; backend.SeArchives = &sounds;
#ifdef SAEKANO_SCENE_CUBISM2
  backend.SceneModels = &models;
#endif
  auto vm = VirtualMachine::FromCompletedProfile(resources, backend, std::move(owner), mode);
  vm->SeedRandom(0);
  const auto entryScript = vm->State().ScriptFileId;
  if (untilMessage) {
    const ScriptObject script(resources.Load(entryScript));
    if (*untilMessage >= script.Records().size() || script.Records()[*untilMessage].Opcode != 0x64)
      throw FormatError("continuation target is not an entry-script message");
  }
  ClockedOrdinaryVoice voices(root);
  auto controls = MakeFreshOrdinaryMouthControl();
  std::map<uint16_t, ProfileMouthPlayback> mouths;
  SpriteMessageMouth spriteMouth(vm->State(), controls, mouths
#ifdef SAEKANO_SCENE_CUBISM2
      ,&models
#endif
  );
  std::unique_ptr<SceneMessageVoiceOwner> voiceOwner;
  int32_t voiceScript = -1;
  const auto sync = [&] {
    if (voiceScript != vm->State().ScriptFileId) {
      if (!vm->State().Random.Known()) vm->SeedRandom(0);
      backend.ResetMessages(); mouths.clear(); controls = MakeFreshOrdinaryMouthControl();
      voiceOwner = std::make_unique<SceneMessageVoiceOwner>(SceneMessageVoiceOwner{
          voices, spriteMouth, MakeFreshOrdinaryVoiceQueue(),
          resources.LoadScriptVoiceEnvelopes(vm->State().ScriptFileId),
          LoadOrdinaryVoiceRangesFromFirst(Read(root / "first.dat")), MakeFreshOrdinaryVoiceSettings()});
      for (const auto& envelope : voiceOwner->Envelopes)
        voiceOwner->Queue.Catalog.push_back(int16_t(envelope.VoiceId));
      backend.BindVoiceOwner(*voiceOwner); voiceScript = vm->State().ScriptFileId;
    }
    for (const auto& [id, actor] : vm->State().Characters) {
      if (!actor.RuntimeKnown) throw UnsupportedCommand("continuation actor runtime is unknown");
      mouths.try_emplace(id);
    }
    std::erase_if(mouths, [&](const auto& item) { return !vm->State().Characters.contains(item.first); });
  };
  const auto policyConfig = SkipAutoEnvironment::Decode(Read(root / "env/env_global.dat"));
  SkipAutoState policy;
  const auto resultFont=GsfFont::Decode(Read(root/"Font.gsf"));
  ArchiveApproachResultHost resultHost(resources,missionVoice,policy,format,timing,
      ModeResultVisualEnvironment::DecodeApproach(Read(root/"env/env_approach.dat")),resultFont,
      TextLayoutMetrics::Decode(Read(root/"env/env_global.dat"),Read(root/"env/env_message_window.dat")),
      uint32_t(GameUtilityFont::Decode(Read(root/"env/env_game_utility.dat")).Size),36);
  backend.ApproachResultSettings=ApproachResultEnvironment::Decode(Read(root/"env/env_approach.dat"));
  backend.ResultConsumer=&resultHost;
  std::optional<uint64_t> firstMessage;
  std::set<uint16_t> opcodes;
  size_t messages = 0, completedMessages = 0, boundaries = 0, audioEvents = 0, spriteSubmissions = 0;
  uint64_t now = 0;
  std::map<uint32_t, int32_t> inlineLanes;
  backend.ActivateLane = [&](uint32_t lane, uint64_t time, int32_t position) {
    const auto before = vm->State().LaneCommandIndex.at(lane);
    vm->ActivateLane(lane);
    const auto after = vm->State().LaneCommandIndex.at(lane);
    inlineLanes[lane] = after;
    out << "{\"kind\":\"inline_queue\",\"now\":" << time << ",\"command\":"
        << vm->State().CurrentCommandIndex << ",\"lane\":" << lane << ",\"voice_position\":"
        << position << ",\"before\":" << before << ",\"after\":" << after << "}\n";
  };
  std::optional<std::pair<int32_t, uint32_t>> previousFilter;
  std::optional<std::tuple<int32_t,int32_t,int32_t>> previousMission;
  std::optional<std::tuple<int32_t,int32_t,bool>> previousResult;
  size_t choiceIndex=0;
  const auto finish = [&](std::string_view reason, bool success) {
    output.Enabled=true;  // A failure before the requested gate still reports its exact boundary.
    out << "{\"kind\":\"result\",\"success\":" << (success ? "true" : "false") << ",\"reason\":";
    String(out, reason);
    out << ",\"now\":" << now << ",\"script\":" << vm->State().ScriptFileId
        << ",\"command\":" << vm->State().CurrentCommandIndex << ",\"opcode\":" << vm->Opcode()
        << ",\"messages_started\":" << messages << ",\"messages_completed\":" << completedMessages
        << ",\"command_boundaries\":" << boundaries << ",\"audio_events\":" << audioEvents
        << ",\"sprite_submissions\":" << spriteSubmissions
        << ",\"mission_voice_id\":" << missionVoice.CurrentId()
        << ",\"mission_voice_frames\":" << missionVoice.FramesConsumed().value_or(0)
        << ",\"choice_plan_entries\":" << approachPlan.size() << ",\"choices_committed\":" << choiceIndex
        << ",\"approach_gauge\":" << vm->State().ApproachGauge
        << ",\"clock_step_ms\":16,\"seed\":0,\"voice_pcm_decoded\":true,\"bgm_se_playback\":false,"
           "\"fresh_unread_controls\":true,\"history_bound\":false,\"gpu_pixels\":false}\n";
  };
  bool confirmationDue=false,confirmationSent=false;
  uint32_t choiceStep=0;
  try {
    for (size_t frame = 0; frame < frameBudget; ++frame) {
      now = uint64_t(frame) * 16;
      SkipAutoInput policyInput;
      if (firstMessage) {
        const auto elapsed = now - *firstMessage;
        policyInput.AutoPressed = elapsed == 96;
        policyInput.AutoHeld = elapsed >= 96 && elapsed < 1296;
      }
      vm->UpdateSkipAuto(policy, policyInput, policyConfig.AutoKeyDuration, now);
      backend.AutoAdvance = policy.Auto;
      voices.Update(now);
      bool ended = false, reached = false, waitingMission=false,waitingSelection=false,waitingResult=false;
      CommandInput commandInput;
      if(choiceIndex<approachPlan.size()) {
        const auto& menu=vm->State().ApproachSelection;
        if(menu.Visible && !menu.PendingPhase) {
          const auto target=approachPlan[choiceIndex];
          const auto found=std::find(menu.OriginalOrdinals.begin(),menu.OriginalOrdinals.end(),target[0]);
          if(found==menu.OriginalOrdinals.end())throw FormatError("planned approach parent is not enabled");
          const auto parent=size_t(found-menu.OriginalOrdinals.begin());
          if(target[1]>=menu.ChildCounts.at(parent) || vm->State().ApproachFlags.at(target[0]*3+target[1]))
            throw FormatError("planned approach child is invalid or already used");
          if(menu.Phase==4) {
            if(menu.ParentUnselected || menu.Parent!=int32_t(parent))commandInput.Approach.Next=true;
            else commandInput.Approach.Accept=true;
          } else if(menu.Phase==7) {
            if(menu.ChildUnselected || menu.Child!=int32_t(target[1]))commandInput.Approach.Next=true;
            else commandInput.Approach.Accept=true;
          }
          if(commandInput.Approach.Next || commandInput.Approach.Accept) {
            ++choiceStep;
            out << "{\"kind\":\"approach_selection_input\",\"step\":" << choiceStep << ",\"now\":" << now
                << ",\"plan_index\":" << choiceIndex << ",\"original_parent\":" << target[0] << ",\"child\":" << target[1]
                << ",\"source\":\"explicit_probe_controller_input\"}\n";
          }
        }
      }
      if(confirmationDue) {
        commandInput.Advance=true;confirmationDue=false;confirmationSent=true;
        out << "{\"kind\":\"mission_confirmation\",\"now\":" << now
            << ",\"source\":\"explicit_probe_Advance\",\"script\":" << vm->State().ScriptFileId
            << ",\"command\":" << vm->State().CurrentCommandIndex << "}\n";
      }
      for (size_t work = 0; work < 256; ++work) {
        sync();
        const auto event = vm->Step(now,commandInput);commandInput.Advance=false;commandInput.Approach={};
        if(event.Opcode==0x3f8 && vm->State().ApproachResult.Started) {
          const auto& r=vm->State().ApproachResult;
          const auto sample=std::tuple(r.Phase,r.PendingPhase.value_or(-1),r.Completed);
          if(previousResult!=sample) {
            out << "{\"kind\":\"approach_result_state\",\"now\":" << now << ",\"script\":" << event.Script
                << ",\"command\":" << event.Command << ",\"phase\":" << r.Phase << ",\"pending\":" << r.PendingPhase.value_or(-1)
                << ",\"done\":" << r.Completed << ",\"outcome\":" << r.Outcome << ",\"gauge\":" << vm->State().ApproachGauge
                << ",\"auto\":" << policy.Auto << ",\"mode\":" << vm->State().SkipMode << ",\"skip_phase\":" << vm->State().SkipPhase
                << ",\"common\":" << vm->State().Effects.CurrentValue << ",\"screen_fade\":" << vm->State().ScreenFade.value_or(-1)
                << ",\"voice\":" << missionVoice.CurrentId() << ",\"voice_ready\":" << missionVoice.Ready()
                << ",\"voice_playing\":" << missionVoice.Playing() << ",\"voice_frames\":" << missionVoice.FramesConsumed().value_or(0)
                << ",\"revealed\":" << resultHost.Text().Reveal.Revealed << ",\"destination\":" << vm->State().PendingCommandIndex
                << ",\"tweens\":[";
            for(size_t i=0;i<7;++i){if(i)out << ',';out << r.Tweens[i].Current;}out << "]}\n";previousResult=sample;
          }
        }
        if(chooseFirstApproach && event.Opcode==0x3f6 && event.Boundary==VmBoundary::CommandCompletedBeforeDispatch) {
          const auto actual=CompletedApproachChoice(vm->State().ApproachSelection);
          if(choiceIndex>=approachPlan.size() || !actual || *actual!=approachPlan[choiceIndex])
            throw FormatError("approach controller did not commit the planned original choice");
          const auto chosen=approachPlan[choiceIndex++];output.Enabled=true;
          out << "{\"kind\":\"approach_selection_committed\",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"now\":" << now << ",\"choice_step\":" << choiceStep
              << ",\"plan_index\":" << choiceIndex-1 << ",\"original_parent\":" << chosen[0] << ",\"child\":" << chosen[1]
              << ",\"destination\":" << vm->State().PendingCommandIndex
              << ",\"used\":[";
          for(size_t i=0;i<18;++i){if(i)out << ',';out << +vm->State().ApproachFlags[i];}out << "]}\n";
          choiceStep=0;
        }
        sync();
        if (event.Boundary == VmBoundary::CommandStarted || event.Boundary == VmBoundary::CommandCompletedBeforeDispatch) {
          ++boundaries;
          if (opcodes.insert(event.Opcode).second)
            out << "{\"kind\":\"opcode\",\"opcode\":" << event.Opcode << ",\"script\":" << event.Script
                << ",\"command\":" << event.Command << ",\"now\":" << now << "}\n";
          if (event.Opcode == 0x3fd || event.Opcode == 0x400)
            out << "{\"kind\":\"filter_command\",\"opcode\":" << event.Opcode
                << ",\"script\":" << event.Script << ",\"command\":" << event.Command
                << ",\"boundary\":" << int(event.Boundary) << ",\"now\":" << now << "}\n";
        }
        if (event.Opcode == 0x3ee && (event.Boundary == VmBoundary::CommandStarted ||
                                     event.Boundary == VmBoundary::CommandCompletedBeforeDispatch)) {
          const auto& s = vm->State();
          out << "{\"kind\":\"approach_entry\",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"boundary\":" << int(event.Boundary)
              << ",\"now\":" << now << ",\"id\":" << s.ApproachMode.Id.value_or(-1)
              << ",\"enabled\":" << s.ApproachMode.Enabled.value_or(false)
              << ",\"gauge\":" << s.ApproachGauge << ",\"day\":" << s.DayEnabled
              << ",\"phase\":" << s.SkipPhase << ",\"textures\":" << approachAssets.Textures().size()
              << ",\"resource_idle\":" << approachAssets.Status().Idle() << "}\n";
        }
#ifdef SAEKANO_SCENE_CUBISM2
        if (event.Opcode == 0x438 && event.Boundary == VmBoundary::CommandCompletedBeforeDispatch) {
          out << "{\"kind\":\"model_command\",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"now\":" << now << ",\"records\":[";
          bool first = true;
          for (const auto& record : vm->State().Live2d.Records) {
            if (!record.ResourceId) continue;
            if (!first) out << ','; first = false;
            const auto model = models.Observe(*record.ResourceId);
            out << "{\"resource\":" << *record.ResourceId << ",\"logical\":" << record.LogicalId
                << ",\"enabled\":" << model.Enabled << ",\"pose\":" << record.Pose.value_or(-1)
                << ",\"position\":[" << model.Position[0] << ',' << model.Position[1]
                << "],\"scale\":[" << model.Scale[0] << ',' << model.Scale[1] << "]}";
          }
          out << "]}\n";
        }
#endif
        if (event.Opcode == 0x64 && (event.Boundary == VmBoundary::CommandStarted ||
                                    event.Boundary == VmBoundary::CommandCompletedBeforeDispatch)) {
          const auto* message = backend.CurrentMessage();
          if (!message) throw FormatError("continuation message state is missing");
          out << "{\"kind\":\"message\",\"boundary\":" << int(event.Boundary)
              << ",\"script\":" << event.Script << ",\"command\":" << event.Command << ",\"now\":" << now
              << ",\"voice\":" << message->VoiceId << ",\"slot\":" << message->VoiceGate.Slot
              << ",\"epoch\":" << message->Epoch << ",\"source_units\":" << message->SourceUnits
              << ",\"revealed\":" << (message->Reveal.Revealed ? "true" : "false") << "}\n";
          if (event.Boundary == VmBoundary::CommandStarted) {
            ++messages;
            if (!firstMessage) firstMessage = now;
            break;
          }
          ++completedMessages;
          if (untilMessage && event.Script == entryScript && size_t(event.Command) == *untilMessage) {
            reached = true; break;
          }
        }
        if(event.Boundary==VmBoundary::WaitingInput && event.Opcode==0x3f7 &&
            vm->State().ApproachMission.Stage==2 && !vm->State().ApproachMission.PendingStage)waitingMission=true;
        if(event.Boundary==VmBoundary::WaitingInput && (event.Opcode==0x65 || event.Opcode==0x3f6))
          waitingSelection=true;
        if(event.Boundary==VmBoundary::WaitingInput && event.Opcode==0x3f8 && vm->State().ApproachResult.Phase==5) {
          const auto& r=vm->State().ApproachResult;
          out << "{\"kind\":\"approach_result_ready\",\"now\":" << now << ",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"phase\":" << r.Phase << ",\"outcome\":" << r.Outcome
              << ",\"voice\":" << r.Selected.Voice << ",\"revealed\":" << resultHost.Text().Reveal.Revealed
              << ",\"mission_frames\":" << missionVoice.FramesConsumed().value_or(0)
              << ",\"pending_destination\":" << vm->State().PendingCommandIndex << "}\n";
          waitingResult=true;
        }
        if (event.Boundary == VmBoundary::WaitingInput || event.Boundary == VmBoundary::WaitingBackend) break;
        if (event.Boundary == VmBoundary::Ended) { ended = true; break; }
        if (work == 255) throw UnsupportedCommand("continuation per-frame command budget exceeded");
      }
      for (const auto& event : vm->TakeEvents()) {
        ++audioEvents;
        if(vm->Opcode()==0x3f8) {
          const bool carryover=now==vm->State().ApproachResult.Tweens[0].Epoch;
          out << "{\"kind\":\"" << (carryover?"approach_result_entry_carryover_event":"approach_result_event")
              << "\",\"now\":" << now << ",\"event\":" << int(event.Kind) << ",\"id\":" << event.Id << "}\n";
        }
        (void)backend.ConsumeVoiceEvent(event);
      }
      for (auto& [lane, previous] : inlineLanes) {
        const auto cursor = vm->State().LaneCommandIndex.at(lane);
        if (cursor != previous) {
          out << "{\"kind\":\"inline_lane_progress\",\"now\":" << now << ",\"lane\":"
              << lane << ",\"cursor\":" << cursor << "}\n";
          previous = cursor;
        }
      }
      vm->UpdateSpriteFrames(uint32_t(now));
      missionVoice.Update(now);
#ifdef SAEKANO_SCENE_CUBISM2
      models.Frame(vm->State().Live2d, now);
#endif
      const auto& mission=vm->State().ApproachMission;
      if(mission.Started) {
        const auto sample=std::tuple(mission.Mission,mission.Stage,mission.PendingStage.value_or(-1));
        if(previousMission!=sample) {
          out << "{\"kind\":\"mission_start\",\"script\":" << vm->State().ScriptFileId
              << ",\"command\":" << vm->State().CurrentCommandIndex << ",\"now\":" << now
              << ",\"id\":" << mission.Mission << ",\"stage\":" << mission.Stage
              << ",\"pending\":" << mission.PendingStage.value_or(-1) << ",\"values\":[";
          for(size_t i=0;i<6;++i){if(i)out << ',';out << mission.Tweens[i].Current;}
          out << "]}\n";previousMission=sample;
        }
      }
      if(waitingResult) {finish("approach_result_confirmation_required",false);return 1;}
      if(waitingMission) {
        if((confirmFirstMission || chooseFirstApproach) && !confirmationSent) {
          output.Enabled=!chooseFirstApproach;confirmationDue=true;
          out << "{\"kind\":\"mission_confirmation_wait\",\"now\":" << now
              << ",\"script\":" << vm->State().ScriptFileId << ",\"command\":"
              << vm->State().CurrentCommandIndex << "}\n";
        } else {finish("approach_mission_confirmation_required",false);return 1;}
      }
      if(waitingSelection) {
        if(vm->Opcode()==0x3f6) {
          const auto& selection=vm->State().ApproachSelection;
          out << "{\"kind\":\"approach_selection_ready\",\"now\":" << now
              << ",\"script\":" << vm->State().ScriptFileId << ",\"command\":" << vm->State().CurrentCommandIndex
              << ",\"phase\":" << selection.Phase << ",\"pending\":" << selection.PendingPhase.value_or(-1)
              << ",\"visible\":" << selection.Visible << ",\"parent\":" << selection.Parent
              << ",\"child\":" << selection.Child << ",\"parent_none\":" << selection.ParentUnselected
              << ",\"child_none\":" << selection.ChildUnselected << ",\"ordinals\":[";
          for(size_t i=0;i<selection.OriginalOrdinals.size();++i){if(i)out << ',';out << selection.OriginalOrdinals[i];}
          out << "],\"children\":[";
          for(size_t i=0;i<selection.ChildCounts.size();++i){if(i)out << ',';out << selection.ChildCounts[i];}
          out << "],\"topic_bank0\":[";
          for(size_t i=50;i<56;++i){if(i!=50)out << ',';out << vm->State().Bank0[i];}
          out << "],\"used\":[";
          for(size_t i=0;i<18;++i){if(i)out << ',';out << +vm->State().ApproachFlags[i];}
          out << "]}\n";
        }
        if(!(choiceIndex<approachPlan.size() && vm->Opcode()==0x3f6)) {
          finish("explicit_choice_required",false);return 1;
        }
      }
      for (const auto& [id, actor] : vm->State().Characters)
        if (actor.Pose.Ready && actor.Tweens[size_t(CharacterTween::Opacity)].Current != 0) mouths.at(id).Update(now);
      backend.UpdateMessage(now);
      const auto& filter = vm->State().ScreenFilter;
      if (!filter.Mode || !filter.Intensity)
        throw UnsupportedCommand("continuation screen filter state is unknown");
      const auto filterSample = std::pair(*filter.Mode, std::bit_cast<uint32_t>(*filter.Intensity));
      if (previousFilter != filterSample) {
        out << "{\"kind\":\"screen_filter\",\"now\":" << now
            << ",\"script\":" << vm->State().ScriptFileId << ",\"command\":" << vm->State().CurrentCommandIndex
            << ",\"mode\":" << filterSample.first << ",\"intensity_bits\":" << filterSample.second << "}\n";
        previousFilter = filterSample;
      }
      if (firstMessage) {
        if (!vm->State().Quake[1]) throw UnsupportedCommand("continuation actor quake is unknown");
        for (auto id : SceneCharacterDrawOrder(vm->State()))
          spriteSubmissions += BuildSceneCharacterPose(vm->State().Characters.at(id), poses,
              vm->SpriteBlink(id)->Phase, mouths.at(id).Frame, *vm->State().Quake[1]).size();
      }
      if (reached) { finish("target_message_completed", true); return 0; }
      if (ended) { finish("script_ended", !untilMessage); return untilMessage ? 2 : 0; }
    }
    finish("frame_budget_exhausted", false); return 2;
  } catch (const std::exception& error) {
    finish(error.what(), false); return 2;
  }
}
}
