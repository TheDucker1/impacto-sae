#include "pcdiagnostics.h"
#include "desktop.h"
#include "musicplayback.h"
#include "../../log.h"
#include "../../profile/profile.h"
#include "../../profile/game.h"
#include "../../profile/games/saekano/game.h"
#include "../../userconfig.h"
#include <charconv>
namespace Impacto::Saekano {
std::optional<int> RunPcDiagnostics(int argc, char* argv[]) {
  if (argc < 2 || !std::string_view(argv[1]).starts_with("--saekano-"))
    return std::nullopt;
  // Development entries use the same profile and file mappings as normal play.
  Profile::Init();
  Profile::ConfigureDefinitions();
  UserConfig::SetActiveGame("saekano");
  Profile::ConfigureGameProfile();
  Profile::Game::Configure();
  Profile::Saekano::Configure();
  Profile::ClearProfile();
  if (argc == 5 && std::string_view(argv[1]) == "--saekano-ui-sounds" &&
      std::string_view(argv[3]) == "--capture")
    return RunUiSoundSession(argv[2], argv[4]);
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-profile-service") {
    uint32_t scene = 0, ordinal = 0, volume = 0;
    if (argc == 6 || (argc == 8 && std::string_view(argv[6]) == "--capture")) {
      const std::string_view a = argv[3], b = argv[4], c = argv[5];
      const auto x = std::from_chars(a.data(), a.data() + a.size(), scene);
      const auto y = std::from_chars(b.data(), b.data() + b.size(), ordinal);
      const auto z = std::from_chars(c.data(), c.data() + c.size(), volume);
      if (x.ec == std::errc{} && x.ptr == a.data() + a.size() && y.ec == std::errc{} &&
          y.ptr == b.data() + b.size() && z.ec == std::errc{} && z.ptr == c.data() + c.size() && volume <= 255)
        return Saekano::RunProfileServiceSession(argv[2], scene, ordinal, uint8_t(volume), argc == 8 ? argv[7] : "");
    }
    ImpLog(LogLevel::Error, LogChannel::General,
        "Usage: --saekano-profile-service USRDIR SCENE ORDINAL VOLUME [--capture DIRECTORY]\n");
    return 1;
  }
  if (argc == 4 && std::string_view(argv[1]) == "--saekano-voice-stop")
    return Saekano::RunOrdinaryVoiceStopSession(argv[2], argv[3]);
  if (argc == 4 && std::string_view(argv[1]) == "--saekano-voice-fade")
    return Saekano::RunVoiceFadeSession(argv[2], argv[3]);
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-ordinary-voice") {
    if (argc == 5 && std::string_view(argv[3]) == "--capture")
      return Saekano::RunOrdinaryVoiceSession(argv[2], argv[4]);
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-ordinary-voice USRDIR --capture DIRECTORY\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-profile-voice") {
    uint32_t character = 0, entry = 0;
    if (argc == 5 || (argc == 7 && std::string_view(argv[5]) == "--capture")) {
      const std::string_view a = argv[3], b = argv[4];
      const auto x = std::from_chars(a.data(), a.data() + a.size(), character);
      const auto y = std::from_chars(b.data(), b.data() + b.size(), entry);
      if (x.ec == std::errc{} && x.ptr == a.data() + a.size() &&
          y.ec == std::errc{} && y.ptr == b.data() + b.size())
        return Saekano::RunProfileVoiceSession(argv[2], character, entry,
                                               argc == 7 ? argv[6] : "");
    }
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-profile-voice USRDIR CHARACTER ENTRY [--capture "
           "DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-se") {
    uint32_t track = 0, channel = 0;
    if (argc == 5 || (argc == 7 && std::string_view(argv[5]) == "--capture")) {
      const std::string_view text = argv[3], channelText = argv[4];
      const auto a = std::from_chars(text.data(), text.data()+text.size(), track);
      const auto b = std::from_chars(channelText.data(), channelText.data()+channelText.size(), channel);
      if (a.ec == std::errc{} && a.ptr == text.data()+text.size() && track <= INT32_MAX &&
          b.ec == std::errc{} && b.ptr == channelText.data()+channelText.size() && channel <= 3)
        return Saekano::RunMusicSession(argv[2], track, argc==7 ? argv[6] : "", channel);
    }
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-se USRDIR TRACK CHANNEL [--capture DIRECTORY]\n");
    return 1;
  }
  if(argc==4 && std::string_view(argv[1])=="--saekano-battle-preview")
    return Saekano::RunBattlePreviewVoiceSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-battle-result-voice")
    return Saekano::RunBattleResultVoiceSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-battle-result-music")
    return Saekano::RunBattleResultMusicSession(argv[2],argv[3]);
  if (argc == 4 && std::string_view(argv[1]) == "--saekano-mission-voice")
    return Saekano::RunMissionVoiceSession(argv[2],argv[3]);
  if (argc == 5 && std::string_view(argv[1]) == "--saekano-mission-voice" && std::string_view(argv[4])=="--prepare")
    return Saekano::RunMissionVoicePrepareSession(argv[2],argv[3]);
  if (argc == 5 && std::string_view(argv[1]) == "--saekano-mission-voice" && std::string_view(argv[4])=="--lifecycle")
    return Saekano::RunMissionVoiceSession(argv[2],argv[3],true);
  if (argc == 5 && std::string_view(argv[1]) == "--saekano-bgm-fade") {
    uint32_t track = 0;
    const std::string_view text(argv[3]);
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),track);
    if (parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && track <= INT32_MAX)
      return Saekano::RunBgmFadeSession(argv[2],track,argv[4]);
  }
  if(argc==4 && std::string_view(argv[1])=="--saekano-bgm-replacement")
    return Saekano::RunBgmReplacementSession(argv[2],argv[3]);
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-audio") {
    uint32_t track = 0;
    if (argc == 4 || (argc == 6 && std::string_view(argv[4]) == "--capture")) {
      const std::string_view text = argv[3];
      const auto parsed = std::from_chars(text.data(),text.data()+text.size(),track);
      if (parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && track <= INT32_MAX)
        return Saekano::RunMusicSession(argv[2],track,argc==6 ? argv[5] : "");
    }
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-audio USRDIR TRACK [--capture DIRECTORY]\n");
    return 1;
  }
    if(argc==3 && std::string_view(argv[1])=="--saekano-result-render-state")
      return Saekano::RunResultRenderStateProbe(argv[2]);
    if (argc == 5 && std::string_view(argv[1]) == "--saekano-particle-group")
      return Saekano::RunParticleGroupProbe(argv[2], argv[3], argv[4]);
    if (argc == 4 && std::string_view(argv[1]) == "--saekano-particle-raster")
      return Saekano::RunParticleRasterProbe(argv[2], argv[3]);
    if (argc == 4 && std::string_view(argv[1]) == "--saekano-particle-fragment")
      return Saekano::RunParticleFragmentProbe(argv[2], argv[3]);
    if (argc == 4 && std::string_view(argv[1]) == "--saekano-particle-projection")
      return Saekano::RunParticleProjectionProbe(argv[2], argv[3]);
    if (argc == 4 && std::string_view(argv[1]) == "--saekano-filter-check")
    return Saekano::RunScreenFilterProbe(argv[2], argv[3]);
  if (argc == 4 && std::string_view(argv[1]) == "--saekano-sdk-ui-check")
    return Saekano::RunSceneSdkUiProbe(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-options-draw-check")
    return Saekano::RunOptionsDrawProbe(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-slot-thumbnail-check")
    return Saekano::RunSlotThumbnailProbe(argv[2],argv[3]);
  if(argc==3 && std::string_view(argv[1])=="--saekano-scene-thumbnail-check")
    return Saekano::RunSceneThumbnailProbe(argv[2]);
  if(argc==5 && std::string_view(argv[1])=="--saekano-title-draw-check")
    return Saekano::RunTitleDrawProbe(argv[2],argv[3],argv[4]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-title-audio-session")
    return Saekano::RunTitleAudioSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-voice-attenuation-session")
    return Saekano::RunVoiceAttenuationSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-audio-owner-session")
    return Saekano::RunAudioOwnerSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-slot-audio-session")
    return Impacto::Saekano::RunSlotReplayAudioSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-options-mixer-session")
    return Saekano::RunOptionsMixerSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-shared-audio-stop-session")
    return Saekano::RunSharedAudioStopSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-options-preview-session")
    return Saekano::RunOptionsPreviewSession(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-movie-info")return Saekano::RunMovieInfoProbe(argv[2],argv[3]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-movie-host-session")return Saekano::RunMovieHostSession(argv[2],argv[3]);
  if(argc==6 && std::string_view(argv[1])=="--saekano-movie-vm-session")
    return Saekano::RunMovieVmSession(argv[2],int32_t(std::stoi(argv[3])),uint32_t(std::stoul(argv[4])),argv[5]);
  if(argc==6 && std::string_view(argv[1])=="--saekano-movie-decode")return Saekano::RunMovieDecodeProbe(argv[2],argv[5],uint32_t(std::stoul(argv[3])),uint32_t(std::stoul(argv[4])));
  if(argc==5 && std::string_view(argv[1])=="--saekano-movie-session")return Saekano::RunMoviePlaybackSession(argv[2],argv[4],uint32_t(std::stoul(argv[3])));
  if(argc==4 && std::string_view(argv[1])=="--saekano-movie-controls-session")return Saekano::RunMoviePlaybackSession(argv[2],argv[3],44,true);
  if(argc>=3 && std::string_view(argv[1])=="--saekano-play"){
    if(argc==9 && std::string_view(argv[3])=="--capture-integration" && std::string_view(argv[5])=="--save-dir" && std::string_view(argv[7])=="--integration-input")
      return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,0,0,{},0,{},argv[8]);
    if((argc==5 || (argc==7 && std::string_view(argv[5])=="--save-dir")) && std::string_view(argv[3])=="--replay-battle"){
      uint32_t row=0;const std::string_view text(argv[4]);
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),row);
      if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())return 1;
      return Saekano::RunTitleScene(argv[2],argc==7?argv[6]:"",{},false,false,0,0,0,0,size_t(row));
    }
    if(argc==7 && std::string_view(argv[3])=="--capture-continue-load" && std::string_view(argv[5])=="--save-dir")
      return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,3);
    if(argc==7 && std::string_view(argv[3])=="--capture-continue-choice" && std::string_view(argv[5])=="--save-dir")
      return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,4);
    if(argc==7 && std::string_view(argv[3])=="--capture-continue-choice-pointer" && std::string_view(argv[5])=="--save-dir")
      return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,5);
    if(argc==7 && std::string_view(argv[3])=="--capture-extras-cg" && std::string_view(argv[5])=="--save-dir")return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,0,0,{},2);
    if(argc==7 && std::string_view(argv[3])=="--capture-extras-movie" && std::string_view(argv[5])=="--save-dir")return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,0,0,{},3);
    if(argc==7 && std::string_view(argv[3])=="--capture-extras-battle" && std::string_view(argv[5])=="--save-dir")return Saekano::RunTitleScene(argv[2],argv[6],argv[4],false,false,0,0,0,0,{},4);
    if(argc==5 && std::string_view(argv[3])=="--capture-extras-music")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,0,0,{},1);
    if(argc==5 && (std::string_view(argv[3])=="--capture-title" || std::string_view(argv[3])=="--capture-title-pointer"))
      return Saekano::RunTitleScene(argv[2],{},argv[4],std::string_view(argv[3])=="--capture-title-pointer");
    if(argc==5 && std::string_view(argv[3])=="--capture-options-pointer")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,4);
    if(argc==5 && std::string_view(argv[3])=="--capture-continue-cancel")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,1);
    if(argc==5 && std::string_view(argv[3])=="--capture-continue-pointer")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,2);
    if(argc==5 && std::string_view(argv[3])=="--capture-options-save")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,1);
    if(argc==5 && std::string_view(argv[3])=="--capture-options-revert")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,2);
    if(argc==5 && std::string_view(argv[3])=="--capture-options-no-save")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,3);
    if(argc==5 && std::string_view(argv[3])=="--capture-opening")return Saekano::RunTitleScene(argv[2],{},argv[4],false,true);
    if(argc==6 && std::string_view(argv[3])=="--capture-new-game-until")
      return Saekano::RunTitleScene(argv[2],{},argv[5],false,true,0,0,0,0,{},0,size_t(std::stoul(argv[4])));
    if(argc==5 && std::string_view(argv[3])=="--capture-menu-save-load")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,0,true);
    if(argc==5 && std::string_view(argv[3])=="--capture-return-title")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,0,2);
    if(argc==5 && std::string_view(argv[3])=="--capture-backlog")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,0,3);
    if(argc==5 && std::string_view(argv[3])=="--capture-message-visibility")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,0,0,0,4);
    if(argc==5 && std::string_view(argv[3])=="--capture-startup")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,3);
    if(argc==5 && std::string_view(argv[3])=="--capture-title-movie")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,true);
    if(argc==5 && std::string_view(argv[3])=="--capture-title-movie-eof")return Saekano::RunTitleScene(argv[2],{},argv[4],false,false,2);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Usage: --saekano-play USRDIR [--save-dir DIRECTORY | --capture-title DIRECTORY | --capture-title-pointer DIRECTORY | --capture-opening DIRECTORY | --capture-title-movie DIRECTORY]");return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-first-scene") {
    if(argc==5 && std::string_view(argv[3])=="--save-dir")
      return Saekano::RunFirstSceneViewer(argv[2],{},false,false,false,{},{},{},false,false,0,{},{},argv[4]);
    if(argc==5 && std::string_view(argv[3])=="--capture-ending-save") {
      const std::filesystem::path output=argv[4];
      return Saekano::RunFirstSceneViewer(argv[2],output,true,false,true,67,{},{},false,false,16,{},{},output/"savedata");
    }
    if(argc==5 && std::string_view(argv[3])=="--capture-particles")
      return Saekano::RunFirstSceneViewer(argv[2],argv[4],true,false,true,{}, {},1,false,false,4);
    if(argc==5 && std::string_view(argv[3])=="--capture-result")
      return Saekano::RunFirstSceneViewer(argv[2],argv[4],true,false,true,{}, {},1,false,false,5);
    if(argc==6 && std::string_view(argv[3])=="--capture-result-plan")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,6,argv[4]);
    if(argc==5 && std::string_view(argv[3])=="--capture-chapter")
      return Saekano::RunFirstSceneViewer(argv[2],argv[4],true,false,true,{}, {},1,false,false,7);
    if(argc==6 && std::string_view(argv[3])=="--capture-profile")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,8,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-lip")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,9,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-battle-entry")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,10,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-battle-dialogue")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,12,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-battle-selection")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,13,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-battle-result")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,14,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-post-battle-ruby")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,15,argv[4]);
    if(argc==6 && std::string_view(argv[3])=="--capture-battle-versus")
      return Saekano::RunFirstSceneViewer(argv[2],argv[5],true,false,true,{}, {},1,false,false,11,argv[4]);
    if ((argc==8 || (argc==9 && (std::string_view(argv[8])=="--wait" || std::string_view(argv[8])=="--confirm" || std::string_view(argv[8])=="--menu" || std::string_view(argv[8])=="--menu-pointer" || std::string_view(argv[8])=="--branch"))) &&
        std::string_view(argv[3])=="--capture-script-range") {
      int32_t script=0;size_t first=0,last=0;
      const auto parse=[](const char* value,auto& output){const std::string_view text(value);
        const auto r=std::from_chars(text.data(),text.data()+text.size(),output);
        return r.ec==std::errc{} && r.ptr==text.data()+text.size();};
      if(parse(argv[4],script) && script>=0 && parse(argv[5],first) && parse(argv[6],last))
        return Saekano::RunFirstSceneViewer(argv[2],argv[7],true,false,true,last,first,script,
            argc==9 && std::string_view(argv[8])=="--wait",argc==9 && std::string_view(argv[8])=="--confirm",
            argc==9?(std::string_view(argv[8])=="--menu"?1:std::string_view(argv[8])=="--menu-pointer"?2:std::string_view(argv[8])=="--branch"?3:0):0);
    }
    if (argc == 7 && std::string_view(argv[3]) == "--capture-range") {
      size_t first = 0, last = 0;
      const std::string_view a(argv[4]), b(argv[5]);
      const auto x = std::from_chars(a.data(), a.data()+a.size(), first);
      const auto y = std::from_chars(b.data(), b.data()+b.size(), last);
      if (x.ec == std::errc{} && x.ptr == a.data()+a.size() &&
          y.ec == std::errc{} && y.ptr == b.data()+b.size())
        return Saekano::RunFirstSceneViewer(argv[2], argv[6], true, false, true, last, first);
    }
    if (argc == 6 && std::string_view(argv[3]) == "--capture-until") {
      size_t target = 0;
      const std::string_view text(argv[4]);
      const auto parsed = std::from_chars(text.data(), text.data()+text.size(), target);
      if (parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size())
        return Saekano::RunFirstSceneViewer(argv[2], argv[5], true, false, true, target);
    }
    if(argc==5 && std::string_view(argv[3])=="--system-save")
      return Saekano::RunFirstSceneViewer(argv[2],{},false,false,false,{},{},{},false,false,0,{},argv[4]);
    if (argc == 3 || (argc == 5 && (std::string_view(argv[3]) == "--capture" ||
                                   std::string_view(argv[3]) == "--capture-auto" ||
                                   std::string_view(argv[3]) == "--capture-auto-button" ||
                                   std::string_view(argv[3]) == "--capture-voiced")))
      return Saekano::RunFirstSceneViewer(
          argv[2], argc == 5 ? argv[4] : "",
          argc == 5 && (std::string_view(argv[3]) == "--capture-auto" || std::string_view(argv[3]) == "--capture-voiced"),
          argc == 5 && std::string_view(argv[3]) == "--capture-auto-button",
          argc == 5 && std::string_view(argv[3]) == "--capture-voiced");
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-first-scene USRDIR [--save-dir DIRECTORY | --system-save FILE | --capture-ending-save DIRECTORY | --capture DIRECTORY | --capture-auto DIRECTORY | --capture-auto-button DIRECTORY | --capture-voiced DIRECTORY | --capture-particles DIRECTORY | --capture-result DIRECTORY | --capture-result-plan PLAN DIRECTORY | --capture-until MESSAGE_COMMAND DIRECTORY | --capture-range FIRST_COMMAND LAST_COMMAND DIRECTORY | --capture-script-range SCRIPT FIRST LAST DIRECTORY [--wait|--confirm|--menu|--menu-pointer|--branch]]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-profile-items") {
    if (argc == 4 ||
        (argc == 6 && (std::string_view(argv[4]) == "--capture" ||
                       std::string_view(argv[4]) == "--capture-navigation" ||
                       std::string_view(argv[4]) == "--capture-dialog" ||
                       std::string_view(argv[4]) == "--capture-flow"))) {
      uint32_t scene = 0;
      const std::string_view text(argv[3]);
      const auto parsed =
          std::from_chars(text.data(), text.data() + text.size(), scene);
      if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size())
        return Saekano::RunProfileItemsViewer(
            argv[2], scene, argc == 6 ? argv[5] : "",
            argc == 6 && std::string_view(argv[4]) == "--capture-navigation",
            argc == 6 && std::string_view(argv[4]) == "--capture-dialog",
            argc == 6 && std::string_view(argv[4]) == "--capture-flow");
    }
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-profile-items USRDIR SCENE [--capture DIRECTORY | "
           "--capture-navigation DIRECTORY | --capture-dialog DIRECTORY | "
           "--capture-flow DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-font") {
    if (argc == 3) return Saekano::RunFontSample(argv[2]);
    if (argc == 5 && std::string_view(argv[3]) == "--capture")
      return Saekano::RunFontSample(argv[2], argv[4]);
    ImpLog(LogLevel::Error, LogChannel::General,
      "Usage: --saekano-font USRDIR [--capture DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-profile-item-frames") {
    if (argc == 4) return Saekano::RunProfileBackdropViewer(argv[2], argv[3], {}, true, true);
    if (argc == 6 && std::string_view(argv[4]) == "--capture")
      return Saekano::RunProfileBackdropViewer(argv[2], argv[3], argv[5], true, true);
    ImpLog(LogLevel::Error, LogChannel::General,
      "Usage: --saekano-profile-item-frames RESOURCE.DAT ENV_OTAKU_MODE.DAT [--capture DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-profile-menu-frame") {
    if (argc == 4) return Saekano::RunProfileBackdropViewer(argv[2], argv[3], {}, true);
    if (argc == 6 && std::string_view(argv[4]) == "--capture")
      return Saekano::RunProfileBackdropViewer(argv[2], argv[3], argv[5], true);
    ImpLog(LogLevel::Error, LogChannel::General,
      "Usage: --saekano-profile-menu-frame RESOURCE.DAT ENV_OTAKU_MODE.DAT [--capture DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-profile-backdrop") {
    if (argc == 4) return Saekano::RunProfileBackdropViewer(argv[2], argv[3]);
    if (argc == 6 && std::string_view(argv[4]) == "--capture")
      return Saekano::RunProfileBackdropViewer(argv[2], argv[3], argv[5]);
    ImpLog(LogLevel::Error, LogChannel::General,
      "Usage: --saekano-profile-backdrop RESOURCE.DAT ENV_OTAKU_MODE.DAT [--capture DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-model-scenes") {
    if (argc == 4) return Saekano::RunModelScenes(argv[2], argv[3]);
    if (argc == 6 && std::string_view(argv[4]) == "--capture")
      return Saekano::RunModelScenes(argv[2], argv[3], argv[5]);
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-model-scenes RESOURCE.DAT ENV_LIVE2D.DAT [--capture DIRECTORY]\n");
    return 1;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "--saekano-model-viewer") {
    if (argc == 3) return Saekano::RunModelViewer(argv[2]);
    if (argc == 5 && std::string_view(argv[3]) == "--capture")
      return Saekano::RunModelViewer(argv[2], argv[4]);
    ImpLog(LogLevel::Error, LogChannel::General,
           "Usage: --saekano-model-viewer RESOURCE.DAT [--capture DIRECTORY]\n");
    return 1;
  }
  return std::nullopt;
}
}  // namespace Impacto::Saekano
