#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Impacto::Saekano {
enum class PcAction { Up,Down,Left,Right,Circle,Cross,Square,Triangle,L,R,Start,Select,Skip,Count };
enum class PcSourceKind { Key,MouseButton,Wheel,GamepadButton,GamepadAxis,JoystickButton,JoystickAxis,JoystickHat };
inline constexpr size_t PcActionCount=size_t(PcAction::Count);
const char* PcActionName(PcAction action);
const char* PcSourceName(PcSourceKind kind);
SDL_Keycode PcActionKey(PcAction action);
struct PcBinding {
  PcAction Action=PcAction::Circle;
  PcSourceKind Kind=PcSourceKind::Key;
  // Empty means any device in this input family. Persist GUID/serial/path,
  // never SDL's per-connection instance ID.
  std::string Device;
  int Code=0,Direction=1;
  uint16_t Modifiers=0;
  float Threshold=.55f,Deadzone=.2f,Rest=0;
};
struct PcInputProfile {
  std::string Name="Default";
  std::vector<PcBinding> Bindings;
  // Recognized controllers normally use their standardized events. An explicit
  // raw selector switches that device to joystick events, preventing duplicates.
  std::vector<std::string> RawDevices;
};
struct PcSettings {
  int Width=960,Height=544;
  bool SkipLogos=false,StrictData=false;
  std::filesystem::path Usrdir;
  std::string ActiveProfile="Default";
  std::vector<PcInputProfile> Profiles;
  static PcSettings Defaults();
  const PcInputProfile& InputProfile()const;
  PcInputProfile& InputProfile();
  void Validate()const;
};
std::string PcPathUtf8(const std::filesystem::path& path);
std::filesystem::path PcPathFromUtf8(const std::string& path);
std::filesystem::path DefaultPcConfigPath();
PcSettings LoadPcSettings(const std::filesystem::path& path);
void SavePcSettings(const std::filesystem::path& path,const PcSettings& settings);
PcSettings& RuntimePcSettings();
void SetRuntimePcSettings(PcSettings settings);
void InitializePcSettings();
// Match the frontend to the SDK selected for this product build.
void ConfigurePcRenderer();
} // namespace Impacto::Saekano
