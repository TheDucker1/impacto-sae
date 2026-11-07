#include "game.h"
#include "titlemenu.h"

#include <cmath>

#include "../../game.h"
#include "../../profile_internal.h"

namespace Impacto::Profile::Saekano {

static std::filesystem::path ReadPath(const char* name) {
  const auto text = EnsureGetMember<std::string>(name);
  if (text.empty()) {
    throw std::runtime_error(
        fmt::format("Saekano profile path {} is empty", name));
  }
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

void Configure() {
  EnsurePushMemberOfType("Saekano", LUA_TTABLE);
  GameDataPath = ReadPath("GameDataPath");
  LauncherAssetsPath = ReadPath("LauncherAssetsPath");
  EnsurePushMemberOfType("Files", LUA_TTABLE);

  constexpr auto names = std::to_array<const char*>({
      "ResourceArchive",
      "FirstArchive",
      "FontFace",
      "FontMetrics",
      "ApproachEnvironment",
      "BacklogEnvironment",
      "ConfigurationEnvironment",
      "DebugEnvironment",
      "DialogEnvironment",
      "StartupEnvironment",
      "UtilityEnvironment",
      "GlobalEnvironment",
      "HelpEnvironment",
      "KeywordEnvironment",
      "Live2dEnvironment",
      "MessageEnvironment",
      "ExtrasEnvironment",
      "OptionsEnvironment",
      "ProfileEnvironment",
      "ParticleEnvironment",
      "SaveManagerEnvironment",
      "SaveLoadEnvironment",
      "TitleEnvironment",
      "ScriptEnvironment",
      "SelectionEnvironment",
      "StringFormatEnvironment",
      "SystemMenuEnvironment",
      "BattleEnvironment",
  });
  static_assert(names.size() == Files.size());
  for (size_t i = 0; i < names.size(); ++i) Files[i] = ReadPath(names[i]);

  Pop();
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
  return gameData / Files.at(static_cast<size_t>(asset));
}

}  // namespace Impacto::Profile::Saekano
