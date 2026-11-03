#pragma once
#include "pcconfig.h"
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace Impacto::Saekano {
struct PcInputDevice {
  SDL_JoystickID Instance=0;
  std::string Name,Guid,Identity;
  SDL_Gamepad* Gamepad=nullptr;
  SDL_Joystick* Joystick=nullptr;
};
struct PcPhysicalInput {
  PcSourceKind Kind=PcSourceKind::Key;
  SDL_JoystickID Instance=0;
  std::string Device,Guid;
  int Code=0;
  float Value=0;
  uint16_t Modifiers=0;
};
bool PcMatchesSource(const PcBinding& binding,const PcPhysicalInput& input);
float PcBindingAmount(const PcBinding& binding,float value);
std::string PcInputLabel(const PcBinding& binding);

// Owns SDL device handles and the complete physical->logical ingress. UI capture
// uses Inspect on the same model; gameplay uses Feed/Poll to emit canonical
// action edges consumed by existing scene owners. Raw events never bypass it.
class PcInputRouter {
 public:
  explicit PcInputRouter(SDL_Window* window,PcInputProfile profile);
  ~PcInputRouter();
  PcInputRouter(const PcInputRouter&)=delete;
  PcInputRouter& operator=(const PcInputRouter&)=delete;
  bool Poll(SDL_Event& output);
  void Feed(const SDL_Event& event);
  std::vector<PcPhysicalInput> Inspect(const SDL_Event& event);
  std::vector<PcPhysicalInput> Snapshot()const;
  void SetProfile(PcInputProfile profile);
  void Reset();
  const std::map<SDL_JoystickID,PcInputDevice>& Devices()const{return devices_;}
  const std::array<bool,PcActionCount>& Held()const{return held_;}
  const PcInputProfile& Profile()const{return profile_;}
  std::string LastError;
 private:
  using SourceKey=std::tuple<PcSourceKind,SDL_JoystickID,int>;
  struct SourceState {PcPhysicalInput Input;std::set<size_t> Bindings;};
  SDL_Window* window_;PcInputProfile profile_;
  std::map<SDL_JoystickID,PcInputDevice> devices_;
  std::map<SourceKey,SourceState> sources_;
  std::set<SourceKey> suppressed_;
  std::array<bool,PcActionCount> held_{};
  std::deque<SDL_Event> output_;
  uint16_t modifiers_=0;
  std::optional<std::pair<SDL_TouchID,SDL_FingerID>> touch_;
  void Open(SDL_JoystickID id);
  void Remove(SDL_JoystickID id);
  bool Raw(const PcInputDevice& d)const;
  void SeedNeutralGate();
  void Recompute();
  void QueueAction(PcAction action,bool down);
  void QueuePointer(bool down,float x,float y);
  void SetSource(PcPhysicalInput input);
};
// Shared capture semantics for the real editor: seed the current neutral rests,
// ignore the click/key already held on entry, capture chords/axis directions,
// then require neutral release before the candidate can be confirmed.
class PcBindingCapture {
 public:
  enum class Stage { Waiting,ReturnNeutral,Ready };
  PcBindingCapture(PcAction action,const PcInputRouter& input,std::string device={});
  void Observe(const PcPhysicalInput& input);
  Stage State=Stage::Waiting;
  std::optional<PcBinding> Candidate;
 private:
  using Key=std::tuple<PcSourceKind,SDL_JoystickID,int>;
  PcAction action_;std::string device_;
  std::map<Key,float> rests_;std::set<Key> blocked_;
  std::optional<PcPhysicalInput> modifier_;
  std::optional<Key> source_;
  void Choose(const PcPhysicalInput& input);
};
} // namespace Impacto::Saekano
