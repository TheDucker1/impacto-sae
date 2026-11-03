#pragma once

#include <array>
#include <filesystem>

namespace Impacto::Profile::Saekano {

enum class GameAsset {
  ResourceArchive,
  FirstArchive,
  FontFace,
  FontMetrics,
  ApproachEnvironment,
  BacklogEnvironment,
  ConfigurationEnvironment,
  DebugEnvironment,
  DialogEnvironment,
  StartupEnvironment,
  UtilityEnvironment,
  GlobalEnvironment,
  HelpEnvironment,
  KeywordEnvironment,
  Live2dEnvironment,
  MessageEnvironment,
  ExtrasEnvironment,
  OptionsEnvironment,
  ProfileEnvironment,
  ParticleEnvironment,
  SaveManagerEnvironment,
  SaveLoadEnvironment,
  TitleEnvironment,
  ScriptEnvironment,
  SelectionEnvironment,
  StringFormatEnvironment,
  SystemMenuEnvironment,
  BattleEnvironment,
  Count,
};

inline bool Configured = false;
inline std::filesystem::path GameDataPath;
inline std::filesystem::path LauncherAssetsPath;
inline std::array<std::filesystem::path, static_cast<size_t>(GameAsset::Count)>
    Files;

void Configure();
std::filesystem::path AssetPath(const std::filesystem::path& gameData,
                                GameAsset asset);

}  // namespace Impacto::Profile::Saekano
