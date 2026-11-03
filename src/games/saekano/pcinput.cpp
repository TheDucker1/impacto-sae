#include "pcinput.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Impacto::Saekano {
namespace {
bool Axis(PcSourceKind k){return k==PcSourceKind::GamepadAxis || k==PcSourceKind::JoystickAxis;}
float SignedAxis(Sint16 value){return float(value)/(value<0?32768.f:32767.f);}
std::string Text(const char* s){return s?s:"";}
uint16_t Modifiers(uint16_t value){
  uint16_t result=0;for(const auto group:{SDL_KMOD_CTRL,SDL_KMOD_SHIFT,SDL_KMOD_ALT,SDL_KMOD_GUI})if(value&group)result|=group;return result;
}
}
bool PcMatchesSource(const PcBinding& b,const PcPhysicalInput& s){
  return b.Kind==s.Kind && b.Code==s.Code && (b.Device.empty() || b.Device==s.Device || b.Device==s.Guid);
}
float PcBindingAmount(const PcBinding& b,float value){
  if(Axis(b.Kind)){
    const float range=b.Direction>0?1-b.Rest:1+b.Rest;
    return range>.0001f?std::clamp((value-b.Rest)*float(b.Direction)/range,0.f,1.f):0;
  }
  if(b.Kind==PcSourceKind::Wheel)return std::clamp(value*float(b.Direction),0.f,1.f);
  return value;
}
std::string PcInputLabel(const PcBinding& b){
  std::string label;
  if(b.Kind==PcSourceKind::Key){
    label=Text(SDL_GetScancodeName(SDL_Scancode(b.Code)));
    if(b.Modifiers&SDL_KMOD_CTRL)label="Ctrl + "+label;
    if(b.Modifiers&SDL_KMOD_SHIFT)label="Shift + "+label;
    if(b.Modifiers&SDL_KMOD_ALT)label="Alt + "+label;
    if(b.Modifiers&SDL_KMOD_GUI)label="GUI + "+label;
  }else if(b.Kind==PcSourceKind::GamepadButton)label=Text(SDL_GetGamepadStringForButton(SDL_GamepadButton(b.Code)));
  else if(b.Kind==PcSourceKind::GamepadAxis)label=Text(SDL_GetGamepadStringForAxis(SDL_GamepadAxis(b.Code)))+(b.Direction>0?" +":" -");
  else if(b.Kind==PcSourceKind::MouseButton)label="Mouse button "+std::to_string(b.Code);
  else if(b.Kind==PcSourceKind::Wheel)label=std::string("Wheel ")+(b.Code?"vertical":"horizontal")+(b.Direction>0?" +":" -");
  else if(b.Kind==PcSourceKind::JoystickHat)label="Hat "+std::to_string(b.Code>>4)+" / "+std::to_string(b.Code&15);
  else label=std::string(PcSourceName(b.Kind))+" "+std::to_string(b.Code)+(Axis(b.Kind)?(b.Direction>0?" +":" -"):"");
  if(label.empty())label=std::string(PcSourceName(b.Kind))+" "+std::to_string(b.Code);
  return label;
}
PcInputRouter::PcInputRouter(SDL_Window* w,PcInputProfile p):window_(w),profile_(std::move(p)){
  int count=0;auto* ids=SDL_GetJoysticks(&count);
  for(int i=0;i<count;++i)Open(ids[i]);SDL_free(ids);
  SDL_PumpEvents();SeedNeutralGate();
}
PcInputRouter::~PcInputRouter(){
  for(auto& [id,d]:devices_){(void)id;if(d.Gamepad)SDL_CloseGamepad(d.Gamepad);else if(d.Joystick)SDL_CloseJoystick(d.Joystick);}
}
bool PcInputRouter::Raw(const PcInputDevice& d)const{
  return !d.Gamepad || std::any_of(profile_.RawDevices.begin(),profile_.RawDevices.end(),[&](const auto& s){return s=="*" || s==d.Identity || s==d.Guid;});
}
void PcInputRouter::Open(SDL_JoystickID id){
  if(devices_.contains(id))return;
  PcInputDevice d;d.Instance=id;
  if(SDL_IsGamepad(id)){d.Gamepad=SDL_OpenGamepad(id);if(d.Gamepad)d.Joystick=SDL_GetGamepadJoystick(d.Gamepad);}
  if(d.Gamepad && !d.Joystick){SDL_CloseGamepad(d.Gamepad);d.Gamepad=nullptr;}
  if(!d.Joystick)d.Joystick=SDL_OpenJoystick(id);
  if(!d.Joystick){LastError="Cannot open input device: "+Text(SDL_GetError());return;}
  char guid[33]{};SDL_GUIDToString(SDL_GetJoystickGUID(d.Joystick),guid,sizeof(guid));d.Guid="guid:"+std::string(guid);
  d.Name=Text(SDL_GetJoystickName(d.Joystick));const auto serial=Text(SDL_GetJoystickSerial(d.Joystick)),path=Text(SDL_GetJoystickPath(d.Joystick));
  d.Identity=d.Guid+(serial.empty()?(path.empty()?"":"|path:"+path):"|serial:"+serial);
  devices_.emplace(id,std::move(d));
}
void PcInputRouter::Remove(SDL_JoystickID id){
  const auto found=devices_.find(id);if(found==devices_.end())return;
  if(touch_ && uint64_t(touch_->first)==(uint64_t(id)|(uint64_t(1)<<63))){QueuePointer(false,0,0);touch_.reset();}
  for(auto i=suppressed_.begin();i!=suppressed_.end();)if(std::get<1>(*i)==id && std::get<0>(*i)!=PcSourceKind::Key && std::get<0>(*i)!=PcSourceKind::MouseButton && std::get<0>(*i)!=PcSourceKind::Wheel)i=suppressed_.erase(i);else ++i;
  for(auto i=sources_.begin();i!=sources_.end();)if(std::get<1>(i->first)==id && std::get<0>(i->first)!=PcSourceKind::Key && std::get<0>(i->first)!=PcSourceKind::MouseButton && std::get<0>(i->first)!=PcSourceKind::Wheel)i=sources_.erase(i);else ++i;
  auto& d=found->second;if(d.Gamepad)SDL_CloseGamepad(d.Gamepad);else SDL_CloseJoystick(d.Joystick);devices_.erase(found);Recompute();
}
void PcInputRouter::QueueAction(PcAction action,bool down){
  SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
  event.key.windowID=SDL_GetWindowID(window_);event.key.key=PcActionKey(action);event.key.down=down;event.key.timestamp=SDL_GetTicksNS();
  // Canonical action event. It is intentionally not a physical scancode.
  output_.push_back(event);
}
void PcInputRouter::QueuePointer(bool down,float x,float y){
  int w=1,h=1;SDL_GetWindowSize(window_,&w,&h);
  SDL_Event event{};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
  event.button.windowID=SDL_GetWindowID(window_);event.button.which=SDL_TOUCH_MOUSEID;event.button.button=SDL_BUTTON_LEFT;event.button.down=down;
  event.button.x=x*float(w);event.button.y=y*float(h);output_.push_back(event);
}
void PcInputRouter::Reset(){
  for(size_t i=0;i<held_.size();++i)if(held_[i])QueueAction(PcAction(i),false);
  held_={};sources_.clear();suppressed_.clear();modifiers_=0;
  if(touch_)QueuePointer(false,0,0);touch_.reset();
}
void PcInputRouter::SetProfile(PcInputProfile p){Reset();profile_=std::move(p);SeedNeutralGate();}
void PcInputRouter::SeedNeutralGate(){
  // Held input crossing GUI/profile/device ownership boundaries must return
  // neutral before it can activate the next owner. Axis rests are per binding.
  for(const auto& s:Snapshot())if(std::any_of(profile_.Bindings.begin(),profile_.Bindings.end(),[&](const auto& b){return PcMatchesSource(b,s) && PcBindingAmount(b,s.Value)>b.Deadzone;}))suppressed_.insert({s.Kind,s.Instance,s.Code});
}
std::vector<PcPhysicalInput> PcInputRouter::Snapshot()const{
  std::vector<PcPhysicalInput> result;
  int n=0;const bool* keys=SDL_GetKeyboardState(&n);for(int i=1;i<n;++i)result.push_back({PcSourceKind::Key,0,"","",i,keys[i]?1.f:0.f,uint16_t(SDL_GetModState())});
  const auto mouse=SDL_GetMouseState(nullptr,nullptr);for(int b=1;b<=32;++b)result.push_back({PcSourceKind::MouseButton,0,"","",b,(mouse&SDL_BUTTON_MASK(b))?1.f:0.f,uint16_t(SDL_GetModState())});
  for(const auto& [id,d]:devices_){
    const auto sample=[&](PcSourceKind k,int c,float v){result.push_back({k,id,d.Identity,d.Guid,c,v});};
    if(Raw(d)){
      for(int i=0;i<SDL_GetNumJoystickButtons(d.Joystick);++i)sample(PcSourceKind::JoystickButton,i,SDL_GetJoystickButton(d.Joystick,i)?1.f:0.f);
      for(int i=0;i<SDL_GetNumJoystickAxes(d.Joystick);++i)sample(PcSourceKind::JoystickAxis,i,SignedAxis(SDL_GetJoystickAxis(d.Joystick,i)));
      for(int i=0;i<SDL_GetNumJoystickHats(d.Joystick);++i){const auto mask=SDL_GetJoystickHat(d.Joystick,i);for(int bit:{1,2,4,8})sample(PcSourceKind::JoystickHat,(i<<4)|bit,(mask&bit)?1.f:0.f);}
    }else{
      for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT;++i)sample(PcSourceKind::GamepadButton,i,SDL_GetGamepadButton(d.Gamepad,SDL_GamepadButton(i))?1.f:0.f);
      for(int i=0;i<SDL_GAMEPAD_AXIS_COUNT;++i)sample(PcSourceKind::GamepadAxis,i,SignedAxis(SDL_GetGamepadAxis(d.Gamepad,SDL_GamepadAxis(i))));
    }
  }
  return result;
}
std::vector<PcPhysicalInput> PcInputRouter::Inspect(const SDL_Event& e){
  std::vector<PcPhysicalInput> result;
  if(e.type==SDL_EVENT_JOYSTICK_ADDED || e.type==SDL_EVENT_GAMEPAD_ADDED){Open(e.jdevice.which);SeedNeutralGate();return result;}
  if(e.type==SDL_EVENT_JOYSTICK_REMOVED || e.type==SDL_EVENT_GAMEPAD_REMOVED){Remove(e.jdevice.which);return result;}
  if(e.type==SDL_EVENT_GAMEPAD_REMAPPED){Remove(e.gdevice.which);Open(e.gdevice.which);SeedNeutralGate();return result;}
  if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP){
    if(e.key.scancode!=SDL_SCANCODE_UNKNOWN && !e.key.repeat)result.push_back({PcSourceKind::Key,0,"","",int(e.key.scancode),e.key.down?1.f:0.f,Modifiers(uint16_t(e.key.mod))});
    return result;
  }
  if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP){
    if(e.button.which!=SDL_TOUCH_MOUSEID)result.push_back({PcSourceKind::MouseButton,0,"","",e.button.button,e.button.down?1.f:0.f,Modifiers(uint16_t(SDL_GetModState()))});return result;
  }
  if(e.type==SDL_EVENT_MOUSE_WHEEL){const float sign=e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.f:1.f;
    if(e.wheel.x)result.push_back({PcSourceKind::Wheel,0,"","",0,e.wheel.x*sign});if(e.wheel.y)result.push_back({PcSourceKind::Wheel,0,"","",1,e.wheel.y*sign});return result;}
  PcSourceKind kind;SDL_JoystickID id=0;int code=0;float value=0;bool raw=false;
  switch(e.type){
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:case SDL_EVENT_GAMEPAD_BUTTON_UP:kind=PcSourceKind::GamepadButton;id=e.gbutton.which;code=e.gbutton.button;value=e.gbutton.down?1.f:0.f;break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:kind=PcSourceKind::GamepadAxis;id=e.gaxis.which;code=e.gaxis.axis;value=SignedAxis(e.gaxis.value);break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:case SDL_EVENT_JOYSTICK_BUTTON_UP:kind=PcSourceKind::JoystickButton;id=e.jbutton.which;code=e.jbutton.button;value=e.jbutton.down?1.f:0.f;raw=true;break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:kind=PcSourceKind::JoystickAxis;id=e.jaxis.which;code=e.jaxis.axis;value=SignedAxis(e.jaxis.value);raw=true;break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:kind=PcSourceKind::JoystickHat;id=e.jhat.which;code=e.jhat.hat;value=float(e.jhat.value);raw=true;break;
    default:return result;
  }
  const auto device=devices_.find(id);if(device==devices_.end() || Raw(device->second)!=raw)return result;
  const auto& d=device->second;
  if(kind==PcSourceKind::JoystickHat){for(int bit:{1,2,4,8})result.push_back({kind,id,d.Identity,d.Guid,(code<<4)|bit,(int(value)&bit)?1.f:0.f});}
  else result.push_back({kind,id,d.Identity,d.Guid,code,value});
  return result;
}
void PcInputRouter::SetSource(PcPhysicalInput input){
  const SourceKey key{input.Kind,input.Instance,input.Code};
  if(suppressed_.contains(key)){
    const bool neutral=std::all_of(profile_.Bindings.begin(),profile_.Bindings.end(),[&](const auto& b){return !PcMatchesSource(b,input) || PcBindingAmount(b,input.Value)<=b.Deadzone;});
    if(!neutral)return;suppressed_.erase(key);
  }
  auto& state=sources_[key];state.Input=std::move(input);Recompute();
}
void PcInputRouter::Recompute(){
  std::array<bool,PcActionCount> held{};
  for(auto& [key,s]:sources_){
    (void)key;
    for(size_t i=0;i<profile_.Bindings.size();++i){const auto& b=profile_.Bindings[i];
      const bool match=PcMatchesSource(b,s.Input) && (modifiers_&b.Modifiers)==b.Modifiers;
      const auto amount=PcBindingAmount(b,s.Input.Value);
      const bool active=match && (s.Bindings.contains(i)?amount>b.Deadzone:amount>=b.Threshold);
      if(active){s.Bindings.insert(i);held[size_t(b.Action)]=true;}else s.Bindings.erase(i);
    }
  }
  for(size_t i=0;i<held.size();++i)if(held[i]!=held_[i])QueueAction(PcAction(i),held[i]);held_=held;
}
void PcInputRouter::Feed(const SDL_Event& e){
  if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){Reset();output_.push_back(e);return;}
  if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED){SeedNeutralGate();output_.push_back(e);return;}
  // Test/capture action events have no physical scancode. They remain logical
  // inputs; real SDL keyboard events always pass through the configured model.
  if((e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) && e.key.scancode==SDL_SCANCODE_UNKNOWN){output_.push_back(e);return;}
  const auto samples=Inspect(e);
  if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP)modifiers_=Modifiers(uint16_t(e.key.mod));
  bool owned=false;
  for(const auto& sample:samples){
    owned|=std::any_of(profile_.Bindings.begin(),profile_.Bindings.end(),[&](const auto& b){return PcMatchesSource(b,sample);});
    SetSource(sample);if(sample.Kind==PcSourceKind::Wheel){auto neutral=sample;neutral.Value=0;SetSource(std::move(neutral));}
  }
  if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP)return;
  if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP){if(owned || e.button.which==SDL_TOUCH_MOUSEID)return;}
  if(e.type==SDL_EVENT_MOUSE_WHEEL && owned)return;
  if(e.type==SDL_EVENT_FINGER_DOWN || e.type==SDL_EVENT_FINGER_MOTION || e.type==SDL_EVENT_FINGER_UP || e.type==SDL_EVENT_FINGER_CANCELED){
    const auto id=std::pair(e.tfinger.touchID,e.tfinger.fingerID);
    if(e.type==SDL_EVENT_FINGER_DOWN && !touch_){touch_=id;QueuePointer(true,e.tfinger.x,e.tfinger.y);}
    else if(touch_==id){if(e.type==SDL_EVENT_FINGER_MOTION){int w=1,h=1;SDL_GetWindowSize(window_,&w,&h);SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;move.motion.windowID=SDL_GetWindowID(window_);move.motion.which=SDL_TOUCH_MOUSEID;move.motion.x=e.tfinger.x*float(w);move.motion.y=e.tfinger.y*float(h);output_.push_back(move);}
      else{QueuePointer(false,e.tfinger.x,e.tfinger.y);touch_.reset();}}
    return;
  }
  if(e.type==SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN || e.type==SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION || e.type==SDL_EVENT_GAMEPAD_TOUCHPAD_UP){
    // The SDL touchpad is normalized positional input, not another fixed button.
    SDL_Event finger{};finger.type=e.type==SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN?SDL_EVENT_FINGER_DOWN:e.type==SDL_EVENT_GAMEPAD_TOUCHPAD_UP?SDL_EVENT_FINGER_UP:SDL_EVENT_FINGER_MOTION;
    finger.tfinger.touchID=SDL_TouchID(uint64_t(e.gtouchpad.which)|(uint64_t(1)<<63));finger.tfinger.fingerID=SDL_FingerID((e.gtouchpad.touchpad<<16)|e.gtouchpad.finger);finger.tfinger.x=e.gtouchpad.x;finger.tfinger.y=e.gtouchpad.y;Feed(finger);return;
  }
  if(e.type>=SDL_EVENT_JOYSTICK_AXIS_MOTION && e.type<=SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED)return;
  output_.push_back(e);
}
bool PcInputRouter::Poll(SDL_Event& result){
  while(output_.empty()){SDL_Event physical{};if(!SDL_PollEvent(&physical))return false;Feed(physical);}
  result=output_.front();output_.pop_front();return true;
}
namespace {
bool Modifier(int c){return c==SDL_SCANCODE_LCTRL || c==SDL_SCANCODE_RCTRL || c==SDL_SCANCODE_LSHIFT || c==SDL_SCANCODE_RSHIFT || c==SDL_SCANCODE_LALT || c==SDL_SCANCODE_RALT || c==SDL_SCANCODE_LGUI || c==SDL_SCANCODE_RGUI;}
uint16_t WithoutOwnModifier(int c,uint16_t mask){
  if(c==SDL_SCANCODE_LCTRL || c==SDL_SCANCODE_RCTRL)return uint16_t(mask&~SDL_KMOD_CTRL);
  if(c==SDL_SCANCODE_LSHIFT || c==SDL_SCANCODE_RSHIFT)return uint16_t(mask&~SDL_KMOD_SHIFT);
  if(c==SDL_SCANCODE_LALT || c==SDL_SCANCODE_RALT)return uint16_t(mask&~SDL_KMOD_ALT);
  if(c==SDL_SCANCODE_LGUI || c==SDL_SCANCODE_RGUI)return uint16_t(mask&~SDL_KMOD_GUI);
  return mask;
}
}
PcBindingCapture::PcBindingCapture(PcAction action,const PcInputRouter& router,std::string device):action_(action),device_(std::move(device)){
  for(const auto& s:router.Snapshot()){
    const Key key{s.Kind,s.Instance,s.Code};rests_[key]=Axis(s.Kind)?s.Value:0;
    if(!Axis(s.Kind) && s.Value>0)blocked_.insert(key);
  }
}
void PcBindingCapture::Choose(const PcPhysicalInput& s){
  const Key key{s.Kind,s.Instance,s.Code};PcBinding b;b.Action=action_;b.Kind=s.Kind;b.Code=s.Code;
  b.Device=device_.empty()?s.Guid:device_;b.Modifiers=s.Modifiers;
  if(Axis(s.Kind)){b.Rest=rests_[key];b.Direction=s.Value>=b.Rest?1:-1;}
  else if(s.Kind==PcSourceKind::Wheel)b.Direction=s.Value>=0?1:-1;
  if(s.Kind==PcSourceKind::Key)b.Modifiers=WithoutOwnModifier(s.Code,b.Modifiers);
  Candidate=std::move(b);source_=key;modifier_.reset();
  State=s.Kind==PcSourceKind::Wheel?Stage::Ready:Stage::ReturnNeutral;
}
void PcBindingCapture::Observe(const PcPhysicalInput& s){
  if(State==Stage::Ready || (!device_.empty() && device_!=s.Device && device_!=s.Guid))return;
  const Key key{s.Kind,s.Instance,s.Code};
  if(blocked_.contains(key)){if(s.Value==0)blocked_.erase(key);return;}
  if(State==Stage::ReturnNeutral){if(source_==key && PcBindingAmount(*Candidate,s.Value)<=Candidate->Deadzone)State=Stage::Ready;return;}
  if(s.Kind==PcSourceKind::Key && Modifier(s.Code)){
    if(s.Value>0)modifier_=s;
    else if(modifier_ && modifier_->Code==s.Code){const auto selected=*modifier_;Choose(selected);State=Stage::Ready;}
    return;
  }
  if(Axis(s.Kind)){
    if(!rests_.contains(key)){rests_[key]=s.Value;return;}
    const float delta=s.Value-rests_[key],range=delta>=0?1-rests_[key]:1+rests_[key];
    if(range>.0001f && std::abs(delta)/range>=.55f)Choose(s);
  }else if(s.Value!=0)Choose(s);
}
} // namespace Impacto::Saekano
