#include "game.h"
#include "titlemenu.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../../game.h"
#include "../../profile_internal.h"

namespace Impacto::Profile::Saekano {

namespace {

struct FileDefinition {
  const char* Name;
  const char* OriginalPath;
};

constexpr auto FileDefinitions = std::to_array<FileDefinition>({
    {"ResourceArchive", "resource.dat"},
    {"FirstArchive", "first.dat"},
    {"FontFace", "DFHSR4.TTC"},
    {"FontMetrics", "Font.gsf"},
    {"ApproachEnvironment", "env/env_approach.dat"},
    {"BacklogEnvironment", "env/env_backlog.dat"},
    {"ConfigurationEnvironment", "env/env_config.dat"},
    {"DebugEnvironment", "env/env_debug.dat"},
    {"DialogEnvironment", "env/env_dialog.dat"},
    {"StartupEnvironment", "env/env_first.dat"},
    {"UtilityEnvironment", "env/env_game_utility.dat"},
    {"GlobalEnvironment", "env/env_global.dat"},
    {"HelpEnvironment", "env/env_help_mgr.dat"},
    {"KeywordEnvironment", "env/env_keyword.dat"},
    {"Live2dEnvironment", "env/env_live2d.dat"},
    {"MessageEnvironment", "env/env_message_window.dat"},
    {"ExtrasEnvironment", "env/env_omake.dat"},
    {"OptionsEnvironment", "env/env_option.dat"},
    {"ProfileEnvironment", "env/env_otaku_mode.dat"},
    {"ParticleEnvironment", "env/env_particle.dat"},
    {"SaveManagerEnvironment", "env/env_save_manager.dat"},
    {"SaveLoadEnvironment", "env/env_saveload.dat"},
    {"TitleEnvironment", "env/env_scene_title.dat"},
    {"ScriptEnvironment", "env/env_script.dat"},
    {"SelectionEnvironment", "env/env_script_select.dat"},
    {"StringFormatEnvironment", "env/env_string_format.dat"},
    {"SystemMenuEnvironment", "env/env_system_menu.dat"},
    {"BattleEnvironment", "env/env_tukkomi.dat"},
    {"BgmBank", "bgm.awb"},
    {"SoundEffectBank", "se.awb"},
    {"VoiceBank", "voice.awb"},
    {"CommentBank", "comment.awb"},
    {"MusicBank", "music.awb"},
    {"SystemVoiceBank", "systemvoice.awb"},
    {"OpeningMovie", "OP1.usm"},
    {"EririEndingMovie", "ED1_ERIRI.usm"},
    {"UtahaEndingMovie", "ED2_UTAHA.usm"},
    {"MichiruEndingMovie", "ED3_MICHIRU.usm"},
    {"MegumiEndingMovie", "ED4_MEGUMI.usm"},
    {"IzumiEndingMovie", "ED5_IZUMI.usm"},
    {"CommonEndingMovie", "ED6_COMMON.usm"},
});
static_assert(FileDefinitions.size() == static_cast<size_t>(GameAsset::Count));

std::array<std::filesystem::path, FileDefinitions.size()> Files;

std::filesystem::path ParsePath(const std::string& text,
                                std::string_view name) {
  if (text.empty()) {
    throw std::runtime_error(
        fmt::format("Saekano profile path {} is empty", name));
  }
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::filesystem::path ResolveMovie(const std::filesystem::path& gameData,
                                   const std::filesystem::path& path) {
  const auto resolved = std::filesystem::weakly_canonical(path);
  const auto relative =
      resolved.lexically_relative(std::filesystem::weakly_canonical(gameData));
  if (relative.empty() || relative == "." || *relative.begin() == "..")
    throw std::runtime_error("Saekano movie resolves outside USRDIR");
  return resolved;
}

}  // namespace

void Configure() {
  EnsurePushMemberOfType("Saekano", LUA_TTABLE);
  GameDataPath =
      ParsePath(EnsureGetMember<std::string>("GameDataPath"), "GameDataPath");
  LauncherAssetsPath = ParsePath(
      EnsureGetMember<std::string>("LauncherAssetsPath"), "LauncherAssetsPath");

  for (size_t i = 0; i < Files.size(); ++i)
    Files[i] = FileDefinitions[i].OriginalPath;
  if (TryPushMember("Files")) {
    AssertIs(LUA_TTABLE);
    ForEachProfileTable<std::string>([](const std::string& name) {
      const auto entry = std::ranges::find_if(
          FileDefinitions, [&](const auto& file) { return name == file.Name; });
      if (entry == FileDefinitions.end())
        throw std::runtime_error(
            fmt::format("Unknown Saekano file role {}", name));
      Files[entry - FileDefinitions.begin()] =
          ParsePath(EnsureGetArrayElement<std::string>(), name);
    });
    Pop();
  }

  TitleMenu::Configure();
  Pop();
  if (!std::isfinite(Game::DesignWidth) || !std::isfinite(Game::DesignHeight) ||
      Game::DesignWidth < 1 || Game::DesignHeight < 1 ||
      Game::DesignWidth > 16384 || Game::DesignHeight > 16384) {
    throw std::runtime_error("Invalid Saekano profile design dimensions");
  }
  Configured = true;
}

std::filesystem::path AssetPath(const std::filesystem::path& gameData,
                                GameAsset asset) {
  if (!Configured) {
    throw std::runtime_error("Saekano game profile has not been configured");
  }
  const auto path = gameData / Files.at(static_cast<size_t>(asset));
  if (asset >= GameAsset::OpeningMovie) return ResolveMovie(gameData, path);
  return path;
}

std::filesystem::path MoviePath(const std::filesystem::path& gameData,
                                const std::filesystem::path& originalName) {
  if (originalName.empty() || originalName.has_root_path() ||
      originalName != originalName.filename() || originalName == "." ||
      originalName == "..")
    throw std::runtime_error("Invalid Saekano movie filename");

  const auto name = originalName.u8string();
  // Native script and gallery entries keep their original names as identifiers.
  for (size_t i = static_cast<size_t>(GameAsset::OpeningMovie);
       i < FileDefinitions.size(); ++i) {
    if (SDL_strcasecmp(reinterpret_cast<const char*>(name.c_str()),
                       FileDefinitions[i].OriginalPath) == 0)
      return AssetPath(gameData, static_cast<GameAsset>(i));
  }
  return ResolveMovie(gameData, gameData / originalName);
}

}  // namespace Impacto::Profile::Saekano
