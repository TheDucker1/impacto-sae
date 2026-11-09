#pragma once

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
  BgmBank,
  SoundEffectBank,
  VoiceBank,
  CommentBank,
  MusicBank,
  SystemVoiceBank,
  OpeningMovie,
  EririEndingMovie,
  UtahaEndingMovie,
  MichiruEndingMovie,
  MegumiEndingMovie,
  IzumiEndingMovie,
  CommonEndingMovie,
  Count,
};

inline bool Configured = false;
inline std::filesystem::path GameDataPath;
inline std::filesystem::path LauncherAssetsPath;

void Configure();
std::filesystem::path AssetPath(const std::filesystem::path& gameData,
                                GameAsset asset);
std::filesystem::path MoviePath(const std::filesystem::path& gameData,
                                const std::filesystem::path& originalName);

}  // namespace Impacto::Profile::Saekano
