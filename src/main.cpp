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
#include "saekano/pcvalidation.h"
#include "saekano/pcdiagnostics.h"
#include <charconv>
#endif

using namespace Impacto;

#ifdef IMPACTO_HAS_SAEKANO
static std::filesystem::path UsrdirOverride,SaveDataOverride;
#endif

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
    if(arg.empty())continue;

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
          if (input.empty() || input[0] == '-') {
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
        make_handler([] {
          std::fputs("Usage: impacto [options]\n"
              "  -g,  --game GAME                  Select a game (saekano starts directly)\n"
              "  -uc, --userconfigpath FILE        User configuration file\n"
              "  -lf, --logfile FILE               Log file\n"
              "  -ll, --loglevel LEVEL             Logging level\n"
              "  -lc, --logchannel CHANNEL         Logging channel\n"
              "  -bp, --basepathspath FILE         Game base-path definitions\n"
              "  -gc, --gamedefinitionspath FILE   Game definitions\n"
              "  -p,  --patch PROFILE              Patch profile\n"
#ifdef IMPACTO_HAS_SAEKANO
              "  -ud, --usrdir DIRECTORY           Saekano data-path override\n"
              "  -sd, --savedatapath DIRECTORY     Saekano save-path override\n"
              "Without a selected game, open the launcher.\n"
              "Example: impacto -g saekano -uc launcher.toml\n"
#endif
              "  -h,  --help                       Show this help\n",stdout);std::exit(0);
        },"-h","--help"),
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
#ifdef IMPACTO_HAS_SAEKANO
        make_handler([&](std::string_view input){UsrdirOverride=Saekano::PcPathFromUtf8(std::string(input));},"-ud","--usrdir"),
        make_handler([&](std::string_view input){SaveDataOverride=Saekano::PcPathFromUtf8(std::string(input));},"-sd","--savedatapath"),
#endif
    };

    const bool handled=std::apply(
        [&](auto&&... h) {
          return ((handleArgInput(h.first, h.second)) || ...);
        },
        argHandlers);
    if(!handled){ImpLog(LogLevel::Fatal,LogChannel::General,"Unknown option: {} (use --help)\n",arg);std::exit(1);}
  }
};

int main(int argc, char* argv[]) {
#ifdef IMPACTO_HAS_SAEKANO
  // Keep the existing explicit GUI entry; normal game selection goes through
  // the same -g/-uc/logging handler table as every other Impacto game.
  if(argc>=2 && std::string_view(argv[1])=="--saekano-launcher"){
    std::filesystem::path config;
    if(argc==4 && std::string_view(argv[2])=="--config")config=Saekano::PcPathFromUtf8(argv[3]);
    else if(argc!=2){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Saekano","Use --saekano-launcher, optionally followed by --config PATH. Direct play: -g saekano [-uc FILE].",nullptr);return 1;}
    return Saekano::RunPcLauncher(config);
  }
  std::vector<std::string_view> legacyArguments;
  if((argc==3 || (argc==5 && std::string_view(argv[3])=="--save-dir")) && std::string_view(argv[1])=="--saekano-play"){
    legacyArguments={"-g","saekano","-ud",argv[2]};if(argc==5){legacyArguments.push_back("-sd");legacyArguments.push_back(argv[4]);}
  }else if((argc==2 || (argc==4 && std::string_view(argv[2])=="--config")) && std::string_view(argv[1])=="--saekano-run"){
    legacyArguments={"-g","saekano"};if(argc==4){legacyArguments.push_back("-uc");legacyArguments.push_back(argv[3]);}
  }
#endif
  if (argc == 5 && std::string_view(argv[1]) == "--saekano-ui-sounds" &&
      std::string_view(argv[3]) == "--capture")
    return Saekano::RunUiSoundSession(argv[2], argv[4]);
#ifdef EMSCRIPTEN
  EM_ASM(OnGameLoadStart(););
#endif
#ifdef _WIN32
  const auto redirected=[](DWORD id){const auto type=GetFileType(GetStdHandle(id));return type==FILE_TYPE_PIPE || type==FILE_TYPE_DISK;};
  const bool stdoutRedirected=redirected(STD_OUTPUT_HANDLE),stderrRedirected=redirected(STD_ERROR_HANDLE),stdinRedirected=redirected(STD_INPUT_HANDLE);
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* fDummy;
    if(!stdoutRedirected)freopen_s(&fDummy, "CONOUT$", "w", stdout);
    if(!stderrRedirected)freopen_s(&fDummy, "CONOUT$", "w", stderr);
    if(!stdinRedirected)freopen_s(&fDummy, "CONIN$", "r", stdin);
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
  if(legacyArguments.empty()){
    if(const auto result=Saekano::RunPcDiagnostics(argc,argv))return *result;
  }
#endif
  std::vector<std::string_view> arguments;
  for (int i = 1; i < argc; ++i) {
    arguments.push_back(argv[i]);
  }
#ifdef IMPACTO_HAS_SAEKANO
  if(!legacyArguments.empty())arguments=legacyArguments;
#endif

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

#ifdef IMPACTO_HAS_SAEKANO
  if(!legacyArguments.empty())ImpLog(LogLevel::Warning,LogChannel::General,"Deprecated Saekano play alias; use -g saekano and the shared options shown by --help.\n");
  const auto config=Saekano::PcPathFromUtf8(UserConfig::UserConfigPath);
  if(UserConfig::GetActiveGame()=="saekano")return Saekano::RunPcConfiguredGame(config,UsrdirOverride,SaveDataOverride);
  if(UserConfig::GetActiveGame().empty()){
    if(!UsrdirOverride.empty() || !SaveDataOverride.empty()){ImpLog(LogLevel::Fatal,LogChannel::General,"Data overrides require -g saekano.\n");return 1;}
    return Saekano::RunPcLauncher(config);
  }
  if(!UsrdirOverride.empty() || !SaveDataOverride.empty()){ImpLog(LogLevel::Fatal,LogChannel::General,"Saekano data overrides do not apply to the selected game.\n");return 1;}
#endif

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
