#include "scenecontinuation.h"
#include "endingdialog.h"

#include "mouthselection.h"
#include "ordinaryvoiceoutput.h"
#include "profilearchive.h"
#include "scenemessage.h"
#include "skipauto.h"
#include "album.h"
#include "missionvoice.h"
#include "approachresulttext.h"
#include "dialog.h"
#include "battleselectionhost.h"
#include "battleresulthost.h"
#include "battledialogue.h"
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
  bool Suppressed=false;
  DiscoveryBuffer(std::streambuf* target,bool enabled):Target(target),Enabled(enabled){}
 protected:
  std::streamsize xsputn(const char* data,std::streamsize size) override {
    return Enabled && !Suppressed?Target->sputn(data,size):size;
  }
  int_type overflow(int_type value) override {
    if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
    return Enabled && !Suppressed?Target->sputc(traits_type::to_char_type(value)):value;
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
                           std::span<const std::array<size_t,2>> approachPlan,bool confirmApproachResult,bool followPostResult,bool followPostChapter,
                           std::span<const std::array<size_t,2>> profilePlan,bool confirmBattleVersus,
                           std::span<const std::array<size_t,3>> battlePlan,bool continueBattle,bool confirmBattleResult,bool followBattleResult,std::optional<int32_t> endingDecision,std::optional<size_t> untilApproachChoices) {
  if(endingDecision && *endingDecision!=0 && *endingDecision!=1)throw FormatError("invalid ending decision");
  const std::array<std::array<size_t,2>,1> firstChoice{{{0,0}}};
  if(chooseFirstApproach && approachPlan.empty())approachPlan=firstChoice;
  chooseFirstApproach=!approachPlan.empty();
  if(untilApproachChoices && (!*untilApproachChoices || *untilApproachChoices>approachPlan.size()))throw FormatError("choice target outside explicit plan");
  DiscoveryBuffer output(report.rdbuf(),!confirmFirstMission && !chooseFirstApproach);std::ostream out(&output);
  output.Suppressed=followPostResult;
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
  InitializeFreshBattleOwner(owner);
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
  ArchiveBattleResources battleAssets(resources);
  ClockedOrdinaryVoice voices(root);
  ClockedMissionVoice missionVoice(root,voices);
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
  backend.BattleResourceBank=&battleAssets;
  backend.BattleConfig=BattleGaugeEnvironment::Decode(Read(root/"env/env_tukkomi.dat"));
  backend.BattleVersusSettings=BattleVersusEnvironment::Decode(Read(root/"env/env_tukkomi.dat"));
  backend.BattleSelectionSettings=BattleSelectionEnvironment::Decode(Read(root/"env/env_tukkomi.dat"));
  backend.BattleResultSettings=BattleResultEnvironment::Decode(Read(root/"env/env_tukkomi.dat"));
  backend.MissionVoiceBank=&missionVoice;
  backend.MissionSettings=ApproachMissionEnvironment::Decode(Read(root/"env/env_approach.dat"));
  backend.SelectionSettings=ApproachSelectionEnvironment::Decode(Read(root/"env/env_approach.dat"));
  backend.ApproachConfig = ApproachGaugeEnvironment::Decode(Read(root / "env/env_approach.dat"));
  backend.ChapterSettings=ChapterEnvironment::Decode(Read(root/"env/env_script.dat"));
  backend.ProfileConsumer=&services;
  backend.ProfileSettings=profile;
  backend.ModeResultSettings=shared;
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
  std::map<uint16_t, ProfileMouthPlayback> mouths;
  SpriteMessageMouth spriteMouth(vm->State(), mouths
#ifdef SAEKANO_SCENE_CUBISM2
      ,&models
#endif
  );
  const auto resultFont=GsfFont::Decode(Read(root/"Font.gsf"));
  const auto metrics=TextLayoutMetrics::Decode(Read(root/"env/env_global.dat"),Read(root/"env/env_message_window.dat"));
  const auto utility=GameUtilityFont::Decode(Read(root/"env/env_game_utility.dat"));
  const auto battleBubble=BattleDialogueEnvironment::Decode(Read(root/"env/env_tukkomi.dat"));
  const auto battleText=OrdinaryTextEnvironment::Decode(Read(root/"env/env_message_window.dat"));
  const auto selectionColors=SharedSelectionEnvironment::Decode(Read(root/"env/env_script_select.dat"));
  const auto widgetInput=WidgetInputEnvironment::DecodeUtility(Read(root/"env/env_game_utility.dat"));
  const auto historySettings=HistoryEnvironment::DecodeDebug(Read(root/"env/env_debug.dat"));
  auto registeredHistory=RegisteredReadHistory::LoadFirst(Read(root/"first.dat"),historySettings.InitiallyRead!=0);
  backend.RegisteredHistory=&registeredHistory;backend.HistorySettings=historySettings;
  AwbBank voiceBank(root/"voice.awb");
  std::unique_ptr<ArchiveBattleSelectionHost> battleHost;
  std::unique_ptr<ArchiveBattleResultHost> battleResultHost;
  SkipAutoState policy;
  backend.ScenePolicy=&policy;
  std::optional<uint64_t> battleFrame;
  std::unique_ptr<SceneMessageVoiceOwner> voiceOwner;
  int32_t voiceScript = -1;
  const auto sync = [&] {
    if (voiceScript != vm->State().ScriptFileId) {
      backend.BattleSelectionConsumerHost=nullptr;battleHost.reset();battleFrame.reset();
      backend.BattleResultConsumerHost=nullptr;battleResultHost.reset();
      if (!vm->State().Random.Known()) vm->SeedRandom(0);
      backend.ResetMessages(); mouths.clear();
      voiceOwner = std::make_unique<SceneMessageVoiceOwner>(SceneMessageVoiceOwner{
          voices, spriteMouth, MakeFreshOrdinaryVoiceQueue(),
          resources.LoadScriptVoiceEnvelopes(vm->State().ScriptFileId),
          LoadOrdinaryVoiceRangesFromFirst(Read(root / "first.dat")), MakeFreshOrdinaryVoiceSettings()});
      auto catalogs=CollectMessageVoiceCatalogs(ScriptObject(resources.Load(vm->State().ScriptFileId)));
      voiceOwner->Queue.Catalog=std::move(catalogs[0]);
      voiceOwner->SecondaryQueue=MakeFreshOrdinaryVoiceQueue();
      voiceOwner->SecondaryQueue->BaseChannel=9;
      voiceOwner->SecondaryQueue->Catalog=std::move(catalogs[1]);
      if(!voiceOwner->SecondaryQueue->Catalog.empty()) {
        out << "{\"kind\":\"split_voice_catalogs\",\"script\":" << vm->State().ScriptFileId << ",\"catalogs\":[";
        for(size_t side=0;side<2;++side){if(side)out << ',';
          const auto& q=side?*voiceOwner->SecondaryQueue:voiceOwner->Queue;
          out << "{\"base\":" << q.BaseChannel << ",\"voices\":[";
          for(size_t i=0;i<q.Catalog.size();++i){if(i)out << ',';out << q.Catalog[i];}out << "]}";}
        out << "]}\n";
      }
      backend.BindVoiceOwner(*voiceOwner); voiceScript = vm->State().ScriptFileId;
      battleHost=std::make_unique<ArchiveBattleSelectionHost>(*backend.BattleSelectionSettings,battleBubble.Bubble,
          widgetInput.HoldFrames,selectionColors,battleText,format,resultFont,metrics,uint32_t(utility.Size),36,
          voiceBank.Index(),*voiceOwner,vm->State(),registeredHistory,historySettings);
      backend.BattleSelectionConsumerHost=battleHost.get();
#ifdef SAEKANO_SCENE_CUBISM2
      battleResultHost=std::make_unique<ArchiveBattleResultHost>(resources,*voiceOwner,models,music,policy);
      backend.BattleResultConsumerHost=battleResultHost.get();
#endif
    }
    for (const auto& [id, actor] : vm->State().Characters) {
      if (!actor.RuntimeKnown) throw UnsupportedCommand("continuation actor runtime is unknown");
      mouths.try_emplace(id);
    }
    std::erase_if(mouths, [&](const auto& item) { return !vm->State().Characters.contains(item.first); });
  };
  const auto policyConfig = SkipAutoEnvironment::Decode(Read(root / "env/env_global.dat"));
  const auto dialogEnvironment=DialogEnvironment::Decode(Read(root/"env/env_dialog.dat"));
  VoiceTextReveal prompt;prompt.PrepareUtf16(DecodeProfileConfirmationPrompt(profileRaw),format,0);
  const auto promptLayout=LayoutPlainText(prompt.Text,format,resultFont,metrics,dialogEnvironment.Settings[6],
      dialogEnvironment.TextScales[1],uint32_t(utility.Size),36);
  if(dialogEnvironment.ButtonGroups.size()<=1)throw FormatError("missing profile confirmation buttons");
  const auto dialogLayout=BuildDialogContentLayout(dialogEnvironment,{480,272},promptLayout.Height,
      dialogEnvironment.ButtonGroups[1].size(),utility.Size);
  DialogState profileDialog;bool profileDialogBound=false;
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
  struct UnboundEndingSave final:EndingSaveServices{
    void Begin(int32_t,uint64_t)override{throw UnsupportedCommand("ending save mode2 persistence service is not bound");}
    EndingSaveStatus Update(uint64_t)override{throw UnsupportedCommand("ending save mode2 persistence service is not bound");}
  }endingSave;
  ScenarioEndingState ending;
  bool endingPulseSent=false;
  std::unique_ptr<ScenarioEndingDialogHost> endingHost;
  if(endingDecision){
    const auto endingEnvironment=EndingEnvironment::Decode(Read(root/"env/env_script.dat"));
    VoiceTextReveal endingPrompt;endingPrompt.PrepareUtf16(endingEnvironment.SystemSavePrompt,format,0);
    const auto layout=LayoutPlainText(endingPrompt.Text,format,resultFont,metrics,dialogEnvironment.Settings[6],
        dialogEnvironment.TextScales[1],uint32_t(utility.Size),36);
    const auto content=BuildDialogContentLayout(dialogEnvironment,{480,272},layout.Height,
        dialogEnvironment.ButtonGroups[1].size(),utility.Size);
    endingHost=std::make_unique<ScenarioEndingDialogHost>(endingEnvironment.SystemSavePrompt,content,
        dialogEnvironment.ButtonGroups[1].size(),endingSave,[&]{
          report << "{\"kind\":\"ending_title_request\",\"now\":" << now << ",\"scene\":1,\"title_constructed\":false}\n";
        });
  }

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
  std::optional<std::pair<int32_t,int32_t>> previousProfile;
  size_t choiceIndex=0,profileIndex=0,battleIndex=0;
  bool battleInputSent=false;
  std::optional<std::array<int32_t,2>> battlePlanDestination;
  std::optional<std::tuple<int32_t,int32_t,size_t>> previousBattleSelection;
  const auto finish = [&](std::string_view reason, bool success) {
    output.Enabled=true;  // A failure before the requested gate still reports its exact boundary.
    output.Suppressed=false;
    out << "{\"kind\":\"result\",\"success\":" << (success ? "true" : "false") << ",\"reason\":";
    String(out, reason);
    out << ",\"now\":" << now << ",\"script\":" << vm->State().ScriptFileId
        << ",\"command\":" << vm->State().CurrentCommandIndex << ",\"opcode\":" << vm->Opcode()
        << ",\"pending_file\":" << vm->State().PendingFileId << ",\"exit_requested\":" << vm->State().ExitRequested;
    if(reason=="script_ended")out << ",\"completion_scope\":\"script_manager_only\",\"outer_handoff_bound\":false";
    if(reason=="ending_title_requested")out << ",\"completion_scope\":\"title_request_only\",\"outer_handoff_bound\":false,\"title_constructed\":false";
    if(ending.Started)out << ",\"ending_controller_bound\":true,\"ending_phase\":" << ending.Phase << ",\"ending_save_backend_bound\":false";

    size_t readSegments=0;
    for(const auto& file:registeredHistory.Files)
      for(const auto segment:file.Segments)if(segment&0x8000)++readSegments;
    out << ",\"read_segment_updates_bound\":true,\"read_segments_marked\":" << readSegments;
    out
        << ",\"messages_started\":" << messages << ",\"messages_completed\":" << completedMessages
        << ",\"command_boundaries\":" << boundaries << ",\"audio_events\":" << audioEvents
        << ",\"sprite_submissions\":" << spriteSubmissions
        << ",\"mission_voice_id\":" << missionVoice.CurrentId()
        << ",\"mission_voice_frames\":" << missionVoice.FramesConsumed().value_or(0)
        << ",\"choice_plan_entries\":" << approachPlan.size() << ",\"choices_committed\":" << choiceIndex
        << ",\"profile_plan_entries\":" << profilePlan.size() << ",\"profiles_committed\":" << profileIndex
        << ",\"approach_gauge\":" << vm->State().ApproachGauge
        << ",\"battle_gauge\":" << vm->State().BattleGauge << ",\"battle_choices_committed\":" << battleIndex
        << ",\"clock_step_ms\":16,\"seed\":0,\"voice_pcm_decoded\":true,\"bgm_se_playback\":false,"
           "\"fresh_unread_controls\":true,\"history_bound\":false,\"gpu_pixels\":false}\n";
  };
  bool confirmationDue=false,confirmationSent=false;
  size_t missionInstance=0;
  bool versusDue=false,versusSent=false;
  bool battleResultDue=false,battleResultSent=false,battleResultDispatched=false;
  std::optional<std::array<int32_t,2>> battleResultDestination;
  std::optional<std::tuple<int32_t,int32_t,size_t>> previousBattleResult;
  std::optional<std::pair<int32_t,int32_t>> previousVersus;
  bool resultConfirmationDue=false,resultConfirmationSent=false,resultCompleted=false,chapterCompleted=false;
  uint32_t choiceStep=0;
  std::optional<std::tuple<int32_t,int32_t,int32_t,int32_t,bool,int32_t>> previousEnding;
  const auto updateEnding=[&]{
    if(ending.Phase==22)endingPulseSent=false;
    auto& modal=endingHost->Modal;
    if(modal.Visible && modal.Phase==3 && !endingPulseSent){
      if(modal.Selected!=*endingDecision)endingHost->Input.Right=true;
      else{endingHost->Input.Accept=true;endingPulseSent=true;}
      report << "{\"kind\":\"ending_prompt_input\",\"now\":" << now << ",\"decision\":" << *endingDecision
          << ",\"right\":" << endingHost->Input.Right << ",\"accept\":" << endingHost->Input.Accept << "}\n";
    }
    UpdateScenarioEnding(ending,*endingHost,now);
    const auto sample=std::tuple(ending.Phase,ending.PendingPhase.value_or(-1),modal.Phase,
        modal.PendingPhase.value_or(-1),modal.Visible,modal.Selected);
    if(previousEnding!=sample || ending.TitleRequested){
      report << "{\"kind\":\"ending_state\",\"now\":" << now << ",\"phase\":" << ending.Phase
          << ",\"pending\":" << ending.PendingPhase.value_or(-1) << ",\"modal_phase\":" << modal.Phase
          << ",\"modal_pending\":" << modal.PendingPhase.value_or(-1) << ",\"modal_visible\":" << modal.Visible
          << ",\"selected\":" << modal.Selected << ",\"title_requested\":" << ending.TitleRequested << ",\"fade\":["
          << ending.Fade.From << ',' << ending.Fade.To << ',' << ending.Fade.Current << ',' << ending.Fade.Duration << ',' << ending.Fade.Epoch << ',' << ending.Fade.Sample << "]}\n";
      previousEnding=sample;
    }
    for(auto sound:modal.UiSounds)report << "{\"kind\":\"ending_ui_sound\",\"now\":" << now << ",\"sound\":" << sound << ",\"physical_playback\":false}\n";
  };

  try {
    for (size_t frame = 0; frame < frameBudget; ++frame) {
      now = uint64_t(frame) * 16;
      if(ending.Started){
        voices.Update(now);updateEnding();
        if(ending.TitleRequested){finish("ending_title_requested",true);return 0;}
        continue;
      }

      SkipAutoInput policyInput;
      if (firstMessage) {
        const auto elapsed = now - *firstMessage;
        policyInput.AutoPressed = elapsed == 96;
        policyInput.AutoHeld = elapsed >= 96 && elapsed < 1296;
      }
      if(!vm->CurrentProfile()) {
        policyInput.HistoryGate=vm->SkipHistoryGate(historySettings,
            &registeredHistory.Files.at(size_t(vm->State().ScriptFileId)));
        vm->UpdateSkipAuto(policy, policyInput, policyConfig.AutoKeyDuration, now);
        backend.AutoAdvance = policy.Auto;
        voices.Update(now);
      }
      bool ended = false, reached = false, waitingMission=false,waitingSelection=false,waitingResult=false,waitingVersus=false,waitingBattleResult=false;
      CommandInput commandInput;
      if(const auto* active=vm->CurrentProfile();active && profileIndex<profilePlan.size()) {
        bool responseVoiceStopped=false;
        const auto target=profilePlan[profileIndex];const auto& menu=active->Selection();
        auto& input=commandInput.Profile;
        if(target[0]>=services.Scene().size() || target[1]>=10)throw FormatError("profile plan is outside loaded menu");
        if(profileDialog.Visible) {
          DialogInput modal;modal.Accept=profileDialog.Phase==3;
          UpdateDialog(profileDialog,modal,now);
          if(!profileDialog.Visible)input.Confirmation=profileDialog.Result==1;
        } else if(active->CurrentStage()==ProfileMode::Stage::Selection && !menu.PendingPhase) {
          if(menu.Phase==5) {
            if(menu.Ordinal==target[0])input.Accept=true;else input.CharacterDirection=true;
          } else if(menu.Phase==6) {
            if(menu.Ordinal!=target[0])throw FormatError("profile plan ordinal differs from active event carousel");
            if(menu.Selectors.at(menu.Ordinal)==target[1])input.Accept=true;else input.EventDirection=true;
          }
        } else if(active->CurrentStage()==ProfileMode::Stage::Result && active->Phase()==12)
          input.Accept=services.ResultText().Revealed && now%32==0;
        else if(active->CurrentStage()==ProfileMode::Stage::Response && active->Phase()==17) {
          const auto status=services.ResponseStatus(now);responseVoiceStopped=status.VoiceStopped;input.Accept=status.TextRevealed && status.VoiceStopped;
        }
        if(input.Accept || input.Confirmation || input.CharacterDirection || input.EventDirection)
          out << "{\"kind\":\"profile_plan_input\",\"now\":" << now << ",\"plan_index\":" << profileIndex
              << ",\"phase\":" << active->Phase() << ",\"accept\":" << input.Accept
              << ",\"confirmation\":" << input.Confirmation.value_or(false)
              << ",\"character_next\":" << input.CharacterDirection.value_or(false)
              << ",\"event_next\":" << input.EventDirection.value_or(false)
              << ",\"result_revealed\":" << services.ResultText().Revealed
              << ",\"response_revealed\":" << services.ResponseText().Revealed
              << ",\"voice_stopped\":" << responseVoiceStopped << "}\n";
      }
      if(resultConfirmationDue) {
        commandInput.Advance=true;resultConfirmationDue=false;resultConfirmationSent=true;
        out << "{\"kind\":\"approach_result_confirmation\",\"now\":" << now
            << ",\"source\":\"explicit_probe_Advance\"}\n";
      }
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
                << ",\"next\":" << commandInput.Approach.Next << ",\"accept\":" << commandInput.Approach.Accept
                << ",\"source\":\"explicit_probe_controller_input\"}\n";
          }
        }
      }
      if(confirmationDue) {
        commandInput.Advance=true;confirmationDue=false;confirmationSent=true;
        report << "{\"kind\":\"mission_confirmation\",\"now\":" << now
            << ",\"instance\":" << missionInstance
            << ",\"source\":\"explicit_probe_Advance\",\"script\":" << vm->State().ScriptFileId
            << ",\"command\":" << vm->State().CurrentCommandIndex << "}\n";
      }
      battleAssets.Pump();
      if(versusDue){commandInput.Advance=true;versusDue=false;versusSent=true;
        out << "{\"kind\":\"battle_versus_confirmation\",\"now\":" << now << ",\"source\":\"explicit_probe_Advance\"}\n";}
      if(battleResultDue){commandInput.Advance=true;battleResultDue=false;battleResultSent=true;
        out << "{\"kind\":\"battle_result_confirmation\",\"now\":" << now << ",\"source\":\"explicit_probe_Advance\"}\n";}
      for (size_t work = 0; work < 256; ++work) {
        sync();
        if(!battleFrame || *battleFrame!=now) {
          uint32_t faces=0;
          if(continueBattle && battleHost->Phase()==4 && vm->Opcode()==0x3f2 && battleIndex>=battlePlan.size()) {
            finish("battle_selection_requires_plan",false);return 1;
          }
          if(battleHost->Phase()==4 && vm->Opcode()==0x3f2 && !battleInputSent && battleIndex<battlePlan.size()) {
            const auto& planned=battlePlan[battleIndex];
            if(planned[0]!=size_t(vm->State().ScriptFileId)||planned[1]!=size_t(vm->State().CurrentCommandIndex)||planned[2]>2)
              throw FormatError("explicit battle plan does not match interactive scene");
            faces=1u<<planned[2];battleInputSent=true;
            out << "{\"kind\":\"battle_selection_face_input\",\"now\":" << now << ",\"script\":" << planned[0]
                << ",\"command\":" << planned[1] << ",\"control\":" << planned[2] << ",\"source\":\"explicit_probe_face_control\"}\n";
          }
          battleHost->BeginFrame(now,{},faces);battleFrame=now;
        }
        const auto event = vm->Step(now,commandInput);commandInput.Advance=false;commandInput.Approach={};
        if(event.Boundary==VmBoundary::CommandStarted && event.Opcode==0x3f7){
          confirmationDue=false;confirmationSent=false;++missionInstance;
          report << "{\"kind\":\"mission_instance_started\",\"instance\":" << missionInstance
              << ",\"now\":" << now << ",\"script\":" << event.Script << ",\"command\":" << event.Command << "}\n";
        }
        if(event.Boundary==VmBoundary::CommandCompletedBeforeDispatch && event.Opcode==0x3f7){
          report << "{\"kind\":\"mission_instance_completed\",\"instance\":" << missionInstance
              << ",\"now\":" << now << ",\"script\":" << event.Script << ",\"command\":" << event.Command
              << ",\"confirmation_sent\":" << confirmationSent << "}\n";
        }
        if(event.Boundary==VmBoundary::CommandStarted && event.Opcode==0x3f8){
          resultConfirmationDue=false;resultConfirmationSent=false;
        }
        if(event.Boundary==VmBoundary::CommandStarted && event.Opcode==0x3f9){
          versusDue=false;versusSent=false;previousVersus.reset();
        }
        if(event.Boundary==VmBoundary::CommandStarted && event.Opcode==0x3fa){
          battleResultDue=false;battleResultSent=false;battleResultDispatched=false;
          battleResultDestination.reset();previousBattleResult.reset();
        }
        if(event.Opcode==0x3fa && vm->State().BattleResult.Started){
          const auto& result=vm->State().BattleResult;const auto sample=std::tuple(result.Phase,result.PendingPhase.value_or(-1),result.Item);
          if(previousBattleResult!=sample || result.Completed){
            out << "{\"kind\":\"battle_result_state\",\"now\":" << now << ",\"script\":" << event.Script << ",\"command\":" << event.Command
                << ",\"phase\":" << result.Phase << ",\"pending_phase\":" << result.PendingPhase.value_or(-1) << ",\"item\":" << result.Item
                << ",\"outcome\":" << result.Outcome << ",\"visible\":" << result.Visible << ",\"done\":" << result.Completed
                << ",\"pending\":" << vm->State().PendingCommandIndex << ",\"fade\":" << vm->State().ScreenFade.value_or(-1)
                << ",\"message_mode\":" << unsigned(vm->State().BattleEntry.MessageModeByte) << ",\"skip\":[" << vm->State().SkipMode << ',' << vm->State().SkipPhase
                << "],\"saved_policy\":[" << unsigned(vm->State().BattleEntry.SavedPolicy->MessageModeByte) << ',' << vm->State().BattleEntry.SavedPolicy->Mode << ',' << vm->State().BattleEntry.SavedPolicy->Phase
                << "],\"continue_auto\":" << *vm->State().ContinueAutoAfterSelection << ",\"continue_skip\":" << *vm->State().ContinueSkipAfterSelection
                << ",\"script_music\":[" << vm->State().BgmTrack.value_or(-1) << ',' << vm->State().BgmCommandVolume.value_or(-1)
                << "],\"voice_frames\":" << voices.FramesConsumed(7).value_or(0) << ",\"voice_playing\":" << voices.Playing(7) << ",\"actors\":[";
            for(size_t i=0;i<vm->State().Live2d.Records.size();++i){if(i)out << ',';const auto& actor=vm->State().Live2d.Records[i];
              out << "{\"logical\":" << actor.LogicalId << ",\"record_pose\":" << actor.Pose.value_or(-1);
#ifdef SAEKANO_SCENE_CUBISM2
              if(actor.ResourceId)out << ",\"expression\":" << models.Observe(*actor.ResourceId).Expression.value_or(SIZE_MAX);
#endif
              out << '}';}
            out << "]}\n";previousBattleResult=sample;
          }
          waitingBattleResult=event.Boundary==VmBoundary::WaitingInput;
          if(event.Boundary==VmBoundary::CommandCompletedBeforeDispatch)battleResultDestination=std::array{event.Script,vm->State().PendingCommandIndex};
        }
        if(battleResultDestination && event.Script==(*battleResultDestination)[0] && event.Command==(*battleResultDestination)[1] && event.Boundary==VmBoundary::BeforeStart){
          out << "{\"kind\":\"battle_result_dispatched\",\"now\":" << now << ",\"script\":" << event.Script << ",\"command\":" << event.Command << "}\n";
          battleResultDispatched=true;
          if(confirmBattleResult && !followBattleResult)break;
          battleResultDestination.reset();
        }
        if(event.Opcode==0x3f2 && vm->State().BattleSelection.Started) {
          const auto& state=battleHost->State();const auto sample=std::tuple(state.Script,state.Phase,state.Ordinal);
          if(previousBattleSelection!=sample || vm->State().BattleSelection.Completed) {
            out << "{\"kind\":\"battle_selection_state\",\"now\":" << now << ",\"script\":" << event.Script
                << ",\"command\":" << event.Command << ",\"phase\":" << state.Phase << ",\"ordinal\":" << state.Ordinal
                << ",\"enabled\":" << state.Enabled << ",\"voice_slot\":" << state.SelectedVoiceSlot
                << ",\"done\":" << vm->State().BattleSelection.Completed << ",\"pending\":" << vm->State().PendingCommandIndex
                << ",\"submissions\":" << battleHost->Draw().size() << ",\"history\":[";
            const auto& rows=registeredHistory.Files.at(size_t(event.Script)).Branches;
            const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& h){return h.Endpoint==event.Command;});
            if(row!=rows.end())for(size_t i=0;i<row->Flags.size();++i){if(i)out << ',';out << unsigned(row->Flags[i]);}
            out << "]}\n";
            previousBattleSelection=sample;
          }
          if(event.Boundary==VmBoundary::CommandCompletedBeforeDispatch && battleInputSent) {
            if(state.Ordinal!=battlePlan[battleIndex][2]+1)throw FormatError("SDK battle committed another ordinal");
            ++battleIndex;battleInputSent=false;
            if(battleIndex==battlePlan.size())battlePlanDestination=std::array{event.Script,vm->State().PendingCommandIndex};
          }
        }
        if(battlePlanDestination && event.Boundary==VmBoundary::BeforeStart &&
           event.Script==(*battlePlanDestination)[0] && event.Command==(*battlePlanDestination)[1]) {
          out << "{\"kind\":\"battle_selection_dispatched\",\"now\":" << now << ",\"script\":" << event.Script
              << ",\"command\":" << event.Command << "}\n";
          if(!continueBattle){finish("battle_plan_complete",true);return 0;}
          battlePlanDestination.reset();
        }
        if(event.Opcode==0x3f9 && vm->State().BattleVersus.Started) {
          const auto& v=vm->State().BattleVersus;const auto sample=std::pair(v.Phase,v.PendingPhase.value_or(-1));
          if(previousVersus!=sample || v.Completed) {
            out << "{\"kind\":\"battle_versus_state\",\"now\":" << now << ",\"script\":" << event.Script
                << ",\"command\":" << event.Command << ",\"phase\":" << v.Phase << ",\"pending\":" << sample.second
                << ",\"done\":" << v.Completed << ",\"visible\":" << v.Visible << ",\"display\":" << vm->State().Battle.Display
                << ",\"fade\":" << vm->State().ScreenFade.value_or(-1) << ",\"tweens\":[";
            for(size_t i=0;i<7;++i){if(i)out << ',';out << v.Tweens[i].Current;}out << "],\"actors\":[";
            for(size_t i=0;i<vm->State().Live2d.Records.size();++i){if(i)out << ',';const auto& r=vm->State().Live2d.Records[i];
              out << '[' << r.LogicalId << ',' << (*r.Position)[0] << ',' << (*r.Position)[1] << ']';}out << "]}\n";
            previousVersus=sample;
          }
          waitingVersus=event.Boundary==VmBoundary::WaitingInput;
        }
        if(followPostResult && resultCompleted && event.Script!=entryScript && output.Suppressed &&
            (!followPostChapter || chapterCompleted) && (profilePlan.empty() || event.Opcode==0x3ed)) {
          output.Suppressed=false;output.Enabled=true;
          out << "{\"kind\":\"post_result_entry\",\"now\":" << now << ",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"prior_range_omitted\":true}\n";
        }
        if(event.Boundary==VmBoundary::ProfileDestinationLoaded) {
          out << "{\"kind\":\"profile_destination\",\"now\":" << now << ",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"plan_index\":" << profileIndex << ",\"stats\":[";
          for(size_t i=0;i<12;++i){if(i)out << ',';out << vm->State().ProfileOwner.Stats[i];}
          out << "]}\n";
          ++profileIndex;previousProfile.reset();profileDialog={};profileDialogBound=false;
        }
        if(event.Opcode==0x2c6 && event.Boundary==VmBoundary::CommandStarted) {
          const auto& control=*vm->State().MouthControl;
          out << "{\"kind\":\"lip_action\",\"now\":" << now << ",\"script\":" << event.Script << ",\"command\":" << event.Command
              << ",\"enabled\":" << control.OverrideEnabled << ",\"ids\":[";
          for(size_t i=0;i<5;++i){if(i)out << ',';out << control.OverrideIds[i];}out << "]}\n";
        }
        if(event.Opcode==0x3ef && (event.Boundary==VmBoundary::CommandStarted || event.Boundary==VmBoundary::CommandCompletedBeforeDispatch)) {
          const auto status=battleAssets.Status();
          out << "{\"kind\":\"battle_entry\",\"now\":" << now << ",\"script\":" << event.Script << ",\"command\":" << event.Command
              << ",\"boundary\":" << int(event.Boundary) << ",\"gauge\":" << vm->State().BattleGauge << ",\"display\":" << vm->State().Battle.Display
              << ",\"sides\":[" << vm->State().Battle.SideCharacters[0] << ',' << vm->State().Battle.SideCharacters[1]
              << "],\"textures\":[" << battleAssets.Textures()[0].size() << ',' << battleAssets.Textures()[1].size()
              << "],\"selection_counts\":[" << vm->State().BattleEntry.SelectLoadedCount << ',' << vm->State().BattleEntry.SelectStartedCount
              << "],\"queued\":" << status.QueuedJobs << ",\"idle\":" << status.Idle() << "}\n";
        }
        if(event.Opcode==0x3ec && event.Boundary==VmBoundary::CommandCompletedBeforeDispatch)
          chapterCompleted=true;
        if(event.Opcode==0x3f8 && event.Boundary==VmBoundary::CommandCompletedBeforeDispatch) {
          resultCompleted=true;
          out << "{\"kind\":\"approach_result_completed\",\"now\":" << now
              << ",\"script\":" << event.Script << ",\"command\":" << event.Command
              << ",\"destination\":" << vm->State().PendingCommandIndex << ",\"auto\":" << policy.Auto
              << ",\"mode\":" << vm->State().SkipMode << ",\"skip_phase\":" << vm->State().SkipPhase << "}\n";
        }
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
          const auto* history=FindChoiceHistory(registeredHistory,event.Script,uint32_t(event.Command));
          out << "{\"kind\":\"approach_choice_history\",\"script\":" << event.Script
              << ",\"command\":" << event.Command << ",\"flags\":[";
          if(history)for(size_t i=0;i<history->Flags.size();++i){if(i)out << ',';out << unsigned(history->Flags[i]);}
          out << "]}\n";
          if(untilApproachChoices && choiceIndex==*untilApproachChoices){finish("target_approach_choice_completed",true);return 0;}
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
              << ",\"revealed\":" << (message->Reveal.Revealed ? "true" : "false")
              << ",\"ruby_count\":" << message->Parsed.Ruby.size()
              << ",\"side\":" << message->Side << ",\"base\":"
              << (message->Side?voiceOwner->SecondaryQueue->BaseChannel:voiceOwner->Queue.BaseChannel)
              << ",\"retained\":[" << bool(backend.MessageSide(0)) << ',' << bool(backend.MessageSide(1)) << ']';
          if(message->VoiceGate.Slot>=0) {
            const auto base=message->Side?voiceOwner->SecondaryQueue->BaseChannel:voiceOwner->Queue.BaseChannel;
            out << ",\"voice_pcm_frames\":" << voices.FramesConsumed(base+uint32_t(message->VoiceGate.Slot)).value_or(0);
          }
          out << "}\n";
          if (event.Boundary == VmBoundary::CommandStarted) {
            ++messages;
            if(confirmApproachResult && !followPostResult && resultCompleted && event.Script!=entryScript) {
              finish("post_result_message_started",true);return 0;
            }
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
        if(vm->State().BattleResult.Started)out << "{\"kind\":\"battle_result_event\",\"now\":" << now << ",\"event\":" << int(event.Kind) << ",\"id\":" << event.Id << "}\n";
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
      if(!vm->CurrentProfile()) {
        vm->UpdateSpriteFrames(uint32_t(now));
        missionVoice.Update(now);
#ifdef SAEKANO_SCENE_CUBISM2
        models.Frame(vm->State().Live2d, now);
#endif
      }
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
      if(waitingVersus) {
        if(confirmBattleVersus && !versusSent)versusDue=true;
        else {finish("battle_versus_confirmation_required",false);return 1;}
      }
      if(battleResultDispatched && confirmBattleResult && !followBattleResult){finish("battle_result_dispatched",true);return 0;}
      if(waitingBattleResult){
        if(confirmBattleResult && !battleResultSent)battleResultDue=true;
        else{finish("battle_result_confirmation_required",false);return 1;}
      }
      if(waitingResult) {
        if(confirmApproachResult && !resultConfirmationSent)resultConfirmationDue=true;
        else {finish("approach_result_confirmation_required",false);return 1;}
      }
      if(waitingMission) {
        if((confirmFirstMission || chooseFirstApproach) && !confirmationSent) {
          output.Enabled=!chooseFirstApproach || (followBattleResult && missionInstance>1);confirmationDue=true;
          report << "{\"kind\":\"mission_confirmation_wait\",\"now\":" << now
              << ",\"instance\":" << missionInstance
              << ",\"script\":" << vm->State().ScriptFileId << ",\"command\":"
              << vm->State().CurrentCommandIndex << "}\n";
        } else {finish("approach_mission_confirmation_required",false);return 1;}
      }
      if(waitingSelection) {
        if(choiceIndex>=approachPlan.size())output.Enabled=true;
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
          out << "],\"enable_bank0\":[";
          for(size_t i=49;i<60;++i){if(i!=49)out << ',';out << vm->State().Bank0[i];}
          out << "],\"used\":[";
          for(size_t i=0;i<18;++i){if(i)out << ',';out << +vm->State().ApproachFlags[i];}
          out << "]}\n";
        }
        if(!(choiceIndex<approachPlan.size() && vm->Opcode()==0x3f6)) {
          finish("explicit_choice_required",false);return 1;
        }
      }
      if(const auto* active=vm->CurrentProfile()) {
        if(active->Selection().ConfirmationOpen && !profileDialogBound) {
          BeginDialog(profileDialog,dialogLayout,dialogEnvironment.ButtonGroups[1].size(),1,false,true,now);
          UpdateDialog(profileDialog,{},now);profileDialogBound=true;
        }
        if(!active->Selection().ConfirmationOpen)profileDialogBound=false;
        const auto sample=std::pair(int32_t(active->CurrentStage()),active->Phase());
        if(previousProfile!=sample) {
          const auto& selection=active->Selection();
          out << "{\"kind\":\"owning_profile_state\",\"now\":" << now << ",\"script\":" << vm->State().ScriptFileId
              << ",\"command\":" << vm->State().CurrentCommandIndex << ",\"mode_id\":" << vm->State().OtakuMode
              << ",\"stage\":" << sample.first << ",\"phase\":" << sample.second
              << ",\"ordinal\":" << selection.Ordinal << ",\"characters\":" << services.Scene().size()
              << ",\"menu_textures\":" << services.MenuTextureCount() << ",\"day\":" << vm->State().DayEnabled << "}\n";
          previousProfile=sample;
        }
        if(active->CurrentStage()==ProfileMode::Stage::Selection &&
            (active->Phase()==5 || active->Phase()==6) && profileIndex>=profilePlan.size()) {
          finish("explicit_profile_choice_required",false);return 1;
        }
        continue; // The profile outer loop owns updates while the script manager is suspended.
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
      if(ended && endingHost){
        output.Enabled=true;output.Suppressed=false;
        report << "{\"kind\":\"script_manager_ended\",\"now\":" << now << ",\"script\":" << vm->State().ScriptFileId
            << ",\"command\":" << vm->State().CurrentCommandIndex << ",\"manager_retained\":true}\n";
        StartScenarioEnding(ending);updateEnding();
        if(ending.TitleRequested){finish("ending_title_requested",true);return 0;}
      }else if(ended){finish("script_ended",!untilMessage);return untilMessage?2:0;}
    }
    finish("frame_budget_exhausted", false); return 2;
  } catch (const std::exception& error) {
    finish(error.what(), false); return 2;
  }
}
}
