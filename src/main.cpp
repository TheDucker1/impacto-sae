#include "impacto.h"
#include <SDL3/SDL_main.h>

#include <ranges>
#include <fmt/format.h>
#include <fmt/ranges.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include "log.h"
#include "game.h"
#include "util.h"

#include "profile/profile.h"
#include "userconfig.h"
#include "io/physicalfilestream.h"
#include "io/filemeta.h"

#ifdef IMPACTO_HAS_SAEKANO
#include "saekano/desktop.h"
#include "saekano/musicplayback.h"
#include <charconv>
#endif

using namespace Impacto;

static uint64_t t;
static float TickDeltaTime() {
  // TODO: Better FPS lock
  uint64_t t2 = SDL_GetPerformanceCounter();
  float dt = (float)(t2 - t) / (float)SDL_GetPerformanceFrequency();
  t = t2;
  return std::min(dt, 1.0f);
}

void GameLoop() {
  float dt = TickDeltaTime();
  Game::Update(dt);
  Game::Render();
}

void LauncherLoop() {
  float dt = TickDeltaTime();
  Game::LauncherUpdate(dt);
  Game::LauncherRender();
}

#ifdef EMSCRIPTEN
extern "C" void EMSCRIPTEN_KEEPALIVE StartGame() {
  t = SDL_GetPerformanceCounter();
  emscripten_set_main_loop(GameLoop, -1, 0);
}
#endif

template <typename T>
concept is_arg_handler =
    std::invocable<T> || std::invocable<T, std::string_view>;

static void HandleArguments(std::vector<std::string_view> args) {
  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view arg = args[i];

    auto handleArgInput = [&](std::ranges::range auto supportedArgs,
                              is_arg_handler auto action) {
      if (std::find(std::begin(supportedArgs), std::end(supportedArgs), arg) !=
          std::end(supportedArgs)) {
        if constexpr (std::invocable<decltype(action), std::string_view>) {
          if (++i >= args.size()) {
            ImpLog(LogLevel::Fatal, LogChannel::General,
                   "Invalid number of arguments");
            exit(1);
          }
          std::string_view input = args[i];
          if (input[0] == '-') {
            ImpLog(LogLevel::Fatal, LogChannel::General,
                   "Missing parameter for {}", arg);
            exit(1);
          }
          action(input);
        } else if constexpr (std::invocable<decltype(action)>) {
          action();
        }
        return true;
      }
      return false;
    };
    using std::literals::string_view_literals::operator""sv;
    constexpr auto make_handler =
        [](is_arg_handler auto fn,
           std::convertible_to<std::string_view> auto... strs) {
          return std::pair{std::to_array<std::string_view>({strs...}), fn};
        };

    const auto argHandlers = std::tuple{
        make_handler(
            [&](std::string_view input) {
              Impacto::UserConfig::CommonSettings.LogFile = std::string(input);
              Impacto::UserConfig::CommonSettings.LoggingToFile = true;
              LogInitFile();
            },
            "-lf", "--logfile"),
        make_handler(
            [&](std::string_view input) {
              std::optional<Impacto::LogChannel> logChannelOpt =
                  StringToChannel(input);
              if (!logChannelOpt) {
                ImpLog(LogLevel::Fatal, LogChannel::General,
                       "Invalid log channel \"{}\", expected one of {}, All!\n",
                       input,
                       fmt::join(magic_enum::enum_names<Impacto::LogChannel>(),
                                 ", "));
                exit(1);
              };
              auto& logChannels =
                  Impacto::UserConfig::CommonSettings.LogChannels;
              if (!Impacto::UserConfig::OverrideLogChannels) {
                logChannels = {};
                Impacto::UserConfig::OverrideLogChannels = true;
              }
              if (*logChannelOpt == LogChannel::None) {
                logChannels = *logChannelOpt;
              } else {
                logChannels |= *logChannelOpt;
              }
            },
            "-lc", "--logchannel"),
        make_handler(
            [&](std::string_view input) {
              auto logLevelOpt =
                  magic_enum::enum_cast<Impacto::LogLevel>(input);
              if (!logLevelOpt) {
                ImpLog(LogLevel::Fatal, LogChannel::General,
                       "Invalid log level \"{}\", expected one of {}!\n", input,
                       magic_enum::enum_names<Impacto::LogLevel>());
                exit(1);
              }
              Impacto::UserConfig::CommonSettings.LogLvl = *logLevelOpt;
            },
            "-ll", "--loglevel"),

        make_handler(
            [&](std::string_view input) {
              UserConfig::SetActiveGame(std::string(input));
            },
            "-g", "--game"),
        make_handler(
            [&](std::string_view input) {
              UserConfig::PatchProfileOverride = input;
            },
            "-p", "--patch"),
        make_handler(
            [&](std::string_view input) { Profile::BasePathsPath = input; },
            "-bp", "--basepathspath"),
        make_handler(
            [&](std::string_view input) {
              Profile::GameDefinitionsPath = input;
            },
            "-gc", "--gamedefinitionspath"),
        make_handler(
            [&](std::string_view input) { UserConfig::UserConfigPath = input; },
            "-uc", "--userconfigpath"),
    };

    std::apply(
        [&](auto&&... h) {
          return ((handleArgInput(h.first, h.second)) || ...);
        },
        argHandlers);
  }
};

int main(int argc, char* argv[]) {
  if (argc == 5 && std::string_view(argv[1]) == "--saekano-ui-sounds" &&
      std::string_view(argv[3]) == "--capture")
    return Saekano::RunUiSoundSession(argv[2], argv[4]);
#ifdef EMSCRIPTEN
  EM_ASM(OnGameLoadStart(););
#endif
#ifdef _WIN32
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
  }
#endif

#if defined(__ANDROID__)
  if (const char* const basePath = SDL_GetAndroidInternalStoragePath())
    std::filesystem::current_path(basePath);
#elif !defined(__SWITCH__)
  // On Switch SDL_GetBasePath is "romfs:/" which we obviously don't want.
  // Homebrew loader correctly sets cwd to the nro directory so it's useless
  // anyway.
  if (const char* const basePath = SDL_GetBasePath())
    std::filesystem::current_path(basePath);
#endif

  std::string profilePath;
  LogInit();

#ifdef IMPACTO_HAS_SAEKANO
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
  if(argc==5 && std::string_view(argv[1])=="--saekano-title-draw-check")
    return Saekano::RunTitleDrawProbe(argv[2],argv[3],argv[4]);
  if(argc==4 && std::string_view(argv[1])=="--saekano-title-audio-session")
    return Saekano::RunTitleAudioSession(argv[2],argv[3]);
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
#endif

  std::vector<std::string_view> arguments;
  for (int i = 1; i < argc; ++i) {
    arguments.push_back(argv[i]);
  }

  HandleArguments(arguments);
  if (arguments.empty() && Io::PathExists("args.txt") == IoError_OK) {
    Io::Stream* stream;
    IoError err = Io::PhysicalFileStream::Create("args.txt", &stream);
    if (err == IoError_OK) {
      std::string fileContents(Io::MemorySize(stream->Meta.Size), '\0');
      stream->Read(fileContents.data(), stream->Meta.Size);
      arguments.clear();
      for (auto&& part : std::views::split(fileContents, ' ')) {
        arguments.emplace_back(
            std::string_view(&*part.begin(), std::ranges::distance(part)));
      }
      HandleArguments(arguments);
      delete stream;
    }
  }

#ifdef EMSCRIPTEN
  // Emscripten's EGL requests a window framebuffer with antialiasing by default
  // (as WebGL does)
  // Emscripten's SDL port fails to change this even with MSAA set to 0 in the
  // context parameters
  EM_ASM(EGL.antialias = false;);
#endif
  try {
    Profile::Init();
    Profile::Configure();
    Game::Init();

#ifdef EMSCRIPTEN
    EM_ASM(OnGameLoaded(););
#else
    t = SDL_GetPerformanceCounter();

    while (!Game::ShouldQuit && UserConfig::GetActiveGame().empty()) {
      LauncherLoop();
    }

    if (!Game::ShouldQuit) {
      Game::InitGameProfile();
    }
    t = SDL_GetPerformanceCounter();
    while (!Game::ShouldQuit) {
      GameLoop();
    }

    ImpLog(LogLevel::Info, LogChannel::General, "Bye!\n");

    Game::Shutdown();
#endif
  } catch (std::exception const& e) {
    ImpLog(LogLevel::Fatal, LogChannel::General,
           "Fatal error occured: {}, exiting!\n", e.what());
    exit(1);
  } catch (...) {
    ImpLog(LogLevel::Fatal, LogChannel::General,
           "Unknown error occured, exiting!\n");
    exit(1);
  }
  return 0;
}
