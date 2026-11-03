#include "pcconfig.h"
#include "pcvalidation.h"
#include "../../profile/game.h"
#include "../../profile/games/saekano/game.h"
#include <toml.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Impacto::Saekano {
namespace {
constexpr std::array<const char*,PcActionCount> Actions={"up","down","left","right","circle","cross","square","triangle","l","r","start","select","skip"};
constexpr std::array<const char*,8> Sources={"key","mouse_button","wheel","gamepad_button","gamepad_axis","joystick_button","joystick_axis","joystick_hat"};
constexpr std::array<SDL_Keycode,PcActionCount> ActionKeys={SDLK_UP,SDLK_DOWN,SDLK_LEFT,SDLK_RIGHT,SDLK_RETURN,SDLK_BACKSPACE,SDLK_SPACE,SDLK_1,SDLK_Q,SDLK_E,SDLK_F13,SDLK_A,SDLK_LCTRL};
bool RuntimeInitialized=false;
template<class T>T Field(const toml::value& v,const char* name,T fallback){return v.contains(name)?toml::find<T>(v,name):fallback;}
template<size_t N>size_t Named(const std::string& name,const std::array<const char*,N>& names){
  const auto found=std::find(names.begin(),names.end(),name);
  if(found==names.end())throw std::runtime_error("Unknown configuration value: "+name);
  return size_t(found-names.begin());
}
}
const char* PcActionName(PcAction a){return Actions.at(size_t(a));}
const char* PcSourceName(PcSourceKind s){return Sources.at(size_t(s));}
SDL_Keycode PcActionKey(PcAction a){return ActionKeys.at(size_t(a));}
std::string PcPathUtf8(const std::filesystem::path& p){const auto s=p.u8string();return {s.begin(),s.end()};}
std::filesystem::path PcPathFromUtf8(const std::string& s){return std::filesystem::path(std::u8string(s.begin(),s.end()));}
PcSettings PcSettings::Defaults(){
  PcSettings result;PcInputProfile profile;
  if (Profile::Saekano::Configured) {
    result.Width = static_cast<int>(Profile::Game::DesignWidth);
    result.Height = static_cast<int>(Profile::Game::DesignHeight);
    std::error_code error;
    if (std::filesystem::is_directory(Profile::Saekano::GameDataPath, error))
      result.Usrdir = Profile::Saekano::GameDataPath;
  }
  const std::array<SDL_Scancode,PcActionCount> keys={SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_Q,SDL_SCANCODE_E,SDL_SCANCODE_RETURN,SDL_SCANCODE_TAB,SDL_SCANCODE_LCTRL};
  const std::array<SDL_GamepadButton,12> buttons={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK};
  for(size_t i=0;i<keys.size();++i)profile.Bindings.push_back({PcAction(i),PcSourceKind::Key,"",int(keys[i])});
  profile.Bindings.push_back({PcAction::Skip,PcSourceKind::Key,"",int(SDL_SCANCODE_RCTRL)});
  for(size_t i=0;i<buttons.size();++i)profile.Bindings.push_back({PcAction(i),PcSourceKind::GamepadButton,"",int(buttons[i])});
  result.Profiles.push_back(std::move(profile));return result;
}
const PcInputProfile& PcSettings::InputProfile()const{
  const auto found=std::find_if(Profiles.begin(),Profiles.end(),[&](const auto& p){return p.Name==ActiveProfile;});
  if(found==Profiles.end())throw std::runtime_error("Active input profile does not exist: "+ActiveProfile);
  return *found;
}
PcInputProfile& PcSettings::InputProfile(){return const_cast<PcInputProfile&>(std::as_const(*this).InputProfile());}
void PcSettings::Validate()const{
  if(Width<1 || Height<1 || Width>16384 || Height>16384)throw std::runtime_error("Display width/height must be positive and at most16384pixels per dimension.");
  if(Profiles.empty() || Profiles.size()>64)throw std::runtime_error("Input requires1..64profiles.");
  std::set<std::string> names;
  for(const auto& p:Profiles){
    if(p.Name.empty() || p.Name.size()>128 || !names.insert(p.Name).second)throw std::runtime_error("Input profile names must be nonempty and unique (maximum128bytes).");
    if(p.Bindings.size()>4096)throw std::runtime_error("Too many input bindings in "+p.Name);
    for(const auto& b:p.Bindings){
      if(size_t(b.Action)>=PcActionCount || size_t(b.Kind)>=Sources.size() || b.Code<0 || b.Code>65535 || (b.Direction!=1 && b.Direction!=-1))throw std::runtime_error("Invalid input source/action in "+p.Name);
      if(b.Direction<0 && b.Kind!=PcSourceKind::GamepadAxis && b.Kind!=PcSourceKind::JoystickAxis && b.Kind!=PcSourceKind::Wheel)throw std::runtime_error("Only axes and wheels have signed binding directions.");
      if(b.Modifiers&~(SDL_KMOD_CTRL|SDL_KMOD_SHIFT|SDL_KMOD_ALT|SDL_KMOD_GUI))throw std::runtime_error("Binding modifiers may contain only Ctrl, Shift, Alt or GUI.");
      if(b.Kind==PcSourceKind::Key && (b.Code==0 || b.Code>=SDL_SCANCODE_COUNT))throw std::runtime_error("Invalid keyboard scancode in "+p.Name);
      if(b.Kind==PcSourceKind::MouseButton && (b.Code<1 || b.Code>32))throw std::runtime_error("Invalid mouse button in "+p.Name);
      if(b.Kind==PcSourceKind::Wheel && b.Code>1)throw std::runtime_error("Wheel axis must be0(horizontal) or1(vertical).");
      if(b.Kind==PcSourceKind::GamepadButton && b.Code>=SDL_GAMEPAD_BUTTON_COUNT)throw std::runtime_error("Invalid standardized gamepad button.");
      if(b.Kind==PcSourceKind::GamepadAxis && b.Code>=SDL_GAMEPAD_AXIS_COUNT)throw std::runtime_error("Invalid standardized gamepad axis.");
      if(b.Kind==PcSourceKind::JoystickHat && !(b.Direction==1 && ((b.Code&15)==SDL_HAT_UP || (b.Code&15)==SDL_HAT_DOWN || (b.Code&15)==SDL_HAT_LEFT || (b.Code&15)==SDL_HAT_RIGHT)))throw std::runtime_error("Hat binding requires a cardinal mask in the low4bits.");
      if(!std::isfinite(b.Threshold) || !std::isfinite(b.Deadzone) || b.Deadzone<0 || b.Threshold<=b.Deadzone || b.Threshold>1)throw std::runtime_error("Axis threshold must exceed dead zone and be at most1.");
      if(!std::isfinite(b.Rest) || b.Rest < -1 || b.Rest > 1)throw std::runtime_error("Axis rest must be within-1..1.");
    }
  }
  if(!names.contains("Default"))throw std::runtime_error("The recoverable Default input profile is required.");
  (void)InputProfile();
}
std::filesystem::path DefaultPcConfigPath(){
  char* path=SDL_GetPrefPath("impacto","Saekano");
  if(!path)throw std::runtime_error(std::string("Cannot locate configuration folder: ")+SDL_GetError());
  const auto result=PcPathFromUtf8(path)/"launcher.toml";SDL_free(path);return result;
}
PcSettings LoadPcSettings(const std::filesystem::path& path){
  if(!std::filesystem::exists(path))return PcSettings::Defaults();
  std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot read configuration: "+PcPathUtf8(path));
  const auto root=toml::parse(input,PcPathUtf8(path));
  if(Field<int>(root,"version",0)!=1)throw std::runtime_error("Unsupported configuration version (expected1).");
  auto s=PcSettings::Defaults();
  s.Width=Field<int>(root,"width",s.Width);s.Height=Field<int>(root,"height",s.Height);
  s.SkipLogos=Field<bool>(root,"skip_logos",false);s.StrictData=Field<bool>(root,"strict_data",false);
  s.Usrdir=PcPathFromUtf8(Field<std::string>(root,"usrdir",{}));s.ActiveProfile=Field<std::string>(root,"active_profile","Default");
  if(root.contains("profiles")){
    s.Profiles.clear();
    for(const auto& value:root.at("profiles").as_array()){
      PcInputProfile p;p.Name=toml::find<std::string>(value,"name");
      if(value.contains("raw_devices"))p.RawDevices=toml::find<std::vector<std::string>>(value,"raw_devices");
      for(const auto& item:value.at("bindings").as_array()){
        PcBinding b;b.Action=PcAction(Named(toml::find<std::string>(item,"action"),Actions));b.Kind=PcSourceKind(Named(toml::find<std::string>(item,"kind"),Sources));
        b.Code=toml::find<int>(item,"code");b.Device=Field<std::string>(item,"device",{});b.Direction=Field<int>(item,"direction",1);
        const auto mods=Field<int>(item,"modifiers",0);if(mods<0 || mods>0xffff)throw std::runtime_error("Invalid keyboard modifier mask.");b.Modifiers=uint16_t(mods);
        b.Threshold=Field<float>(item,"threshold",.55f);b.Deadzone=Field<float>(item,"deadzone",.2f);b.Rest=Field<float>(item,"rest",0);p.Bindings.push_back(std::move(b));
      }
      s.Profiles.push_back(std::move(p));
    }
  }
  s.Validate();return s;
}
void SavePcSettings(const std::filesystem::path& path,const PcSettings& s){
  s.Validate();PcRequireOutsideData(path,s.Usrdir);toml::value root(toml::table{});
  root["version"]=1;root["width"]=s.Width;root["height"]=s.Height;root["skip_logos"]=s.SkipLogos;root["strict_data"]=s.StrictData;
  root["usrdir"]=PcPathUtf8(s.Usrdir);root["active_profile"]=s.ActiveProfile;toml::array profiles;
  for(const auto& p:s.Profiles){
    toml::value v(toml::table{});v["name"]=p.Name;v["raw_devices"]=p.RawDevices;toml::array bindings;
    for(const auto& b:p.Bindings){toml::value x(toml::table{});x["action"]=std::string(PcActionName(b.Action));x["kind"]=std::string(PcSourceName(b.Kind));
      x["device"]=b.Device;x["code"]=b.Code;x["direction"]=b.Direction;x["modifiers"]=int(b.Modifiers);x["threshold"]=double(b.Threshold);x["deadzone"]=double(b.Deadzone);x["rest"]=double(b.Rest);bindings.push_back(std::move(x));}
    v["bindings"]=std::move(bindings);profiles.push_back(std::move(v));
  }
  root["profiles"]=std::move(profiles);
  const auto parent=path.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
  auto temp=path;temp+=".tmp-"+std::to_string(SDL_GetTicksNS());
  try{
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.exceptions(std::ios::failbit|std::ios::badbit);out<<"# Saekano PC launcher configuration. Physical bindings drive logical Vita actions.\n"<<toml::format(root);out.flush();out.close();}
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace configuration (Windows error "+std::to_string(GetLastError())+").");
#else
    std::filesystem::rename(temp,path);
#endif
  }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
PcSettings& RuntimePcSettings(){static auto settings=PcSettings::Defaults();return settings;}
void SetRuntimePcSettings(PcSettings s){s.Validate();RuntimePcSettings()=std::move(s);RuntimeInitialized=true;}
void InitializePcSettings(){if(!RuntimeInitialized)SetRuntimePcSettings(LoadPcSettings(DefaultPcConfigPath()));}
} // namespace Impacto::Saekano
