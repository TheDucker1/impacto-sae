#include "scenecontinuation.h"

#include "mouthselection.h"
#include "ordinaryvoiceoutput.h"
#include "profilearchive.h"
#include "scenemessage.h"
#include "skipauto.h"

#include <fstream>
#include <bit>
#include <iterator>
#include <ostream>
#include <set>

namespace Impacto::Saekano {
namespace {
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
                         std::optional<size_t> untilMessage, std::ostream& out) {
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
  ArchiveCharacterPoses poses(resources);
  ArchiveBgmResources music(root);
  ArchiveSeResources sounds(root);
  SceneMessageBackend backend(format, timing, *owner.TextSpeed, owner.AutoSpeed);
  backend.AutoReveal = backend.AutoAdvance = false;
  backend.SceneImages = &images; backend.CharacterPoses = &poses;
  backend.MusicResources = &music; backend.SeArchives = &sounds;
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
  SpriteMessageMouth spriteMouth(vm->State(), controls, mouths);
  std::unique_ptr<SceneMessageVoiceOwner> voiceOwner;
  int32_t voiceScript = -1;
  const auto sync = [&] {
    if (voiceScript != vm->State().ScriptFileId) {
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
  std::optional<uint64_t> firstMessage;
  std::set<uint16_t> opcodes;
  size_t messages = 0, completedMessages = 0, boundaries = 0, audioEvents = 0, spriteSubmissions = 0;
  uint64_t now = 0;
  std::optional<std::pair<int32_t, uint32_t>> previousFilter;
  const auto finish = [&](std::string_view reason, bool success) {
    out << "{\"kind\":\"result\",\"success\":" << (success ? "true" : "false") << ",\"reason\":";
    String(out, reason);
    out << ",\"now\":" << now << ",\"script\":" << vm->State().ScriptFileId
        << ",\"command\":" << vm->State().CurrentCommandIndex << ",\"opcode\":" << vm->Opcode()
        << ",\"messages_started\":" << messages << ",\"messages_completed\":" << completedMessages
        << ",\"command_boundaries\":" << boundaries << ",\"audio_events\":" << audioEvents
        << ",\"sprite_submissions\":" << spriteSubmissions
        << ",\"clock_step_ms\":16,\"seed\":0,\"voice_pcm_decoded\":true,\"bgm_se_playback\":false,"
           "\"fresh_unread_controls\":true,\"history_bound\":false,\"gpu_pixels\":false}\n";
  };
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
      bool ended = false, reached = false;
      for (size_t work = 0; work < 256; ++work) {
        sync();
        const auto event = vm->Step(now);
        sync();
        if (event.Boundary == VmBoundary::CommandStarted || event.Boundary == VmBoundary::CommandCompletedBeforeDispatch) {
          ++boundaries;
          if (opcodes.insert(event.Opcode).second)
            out << "{\"kind\":\"opcode\",\"opcode\":" << event.Opcode << ",\"script\":" << event.Script
                << ",\"command\":" << event.Command << ",\"now\":" << now << "}\n";
        }
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
        if (event.Boundary == VmBoundary::WaitingInput || event.Boundary == VmBoundary::WaitingBackend) break;
        if (event.Boundary == VmBoundary::Ended) { ended = true; break; }
        if (work == 255) throw UnsupportedCommand("continuation per-frame command budget exceeded");
      }
      audioEvents += vm->TakeEvents().size();
      vm->UpdateSpriteFrames(uint32_t(now));
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
