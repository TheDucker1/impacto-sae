#include "pcvalidation.h"
#include "pcinput.h"
#include "desktop.h"
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_custom/backends/imgui_impl_opengl3.h>
#include <misc/cpp/imgui_stdlib.h>
#include <pugixml.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace Impacto::Saekano {
namespace {
void Require(bool result,const char* operation){if(!result)throw std::runtime_error(std::string(operation)+": "+SDL_GetError());}
ImVec2 Add(ImVec2 a,ImVec2 b){return {a.x+b.x,a.y+b.y};}
float Units(float value){return value*ImGui::GetStyle().FontScaleDpi;}
struct Svg {
  enum class Kind {Rect,Circle,Line,Polygon,Polyline};
  struct Shape{Kind Type;ImU32 Fill=0,Stroke=0;float Width=0,X=0,Y=0,W=0,H=0,Radius=0;std::vector<ImVec2> Points;};
  float Width=64,Height=64;std::vector<Shape> Shapes;
  static ImU32 Color(const std::string& text,ImU32 fallback){
    if(text.empty())return fallback;if(text=="none")return 0;
    if(text.size()!=7 || text[0]!='#')throw std::runtime_error("Launcher SVG requires #RRGGBB colors or none.");
    const auto value=std::stoul(text.substr(1),nullptr,16);return IM_COL32((value>>16)&255,(value>>8)&255,value&255,255);
  }
  explicit Svg(const std::filesystem::path& path){
    pugi::xml_document doc;if(!doc.load_file(path.c_str()))throw std::runtime_error("Cannot load required SVG: "+PcPathUtf8(path));
    const auto root=doc.child("svg");if(!root)throw std::runtime_error("Invalid launcher SVG: "+PcPathUtf8(path));
    float x=0,y=0;std::istringstream box(root.attribute("viewBox").value());
    if(!(box>>x>>y>>Width>>Height) || x!=0 || y!=0 || Width<=0 || Height<=0)throw std::runtime_error("Invalid launcher SVG viewBox.");
    Read(root,Color(root.attribute("fill").value(),0),Color(root.attribute("stroke").value(),0),root.attribute("stroke-width").as_float(1));
  }
  void Read(const pugi::xml_node& root,ImU32 fill,ImU32 stroke,float width){
    for(const auto& node:root.children()){
      if(node.type()!=pugi::node_element)continue;
      const std::string name=node.name();const auto f=Color(node.attribute("fill").value(),fill),s=Color(node.attribute("stroke").value(),stroke);
      const auto thickness=node.attribute("stroke-width").as_float(width);
      if(name=="g"){Read(node,f,s,thickness);continue;}
      Shape shape;shape.Fill=f;shape.Stroke=s;shape.Width=thickness;
      if(name=="rect"){shape.Type=Kind::Rect;shape.X=node.attribute("x").as_float();shape.Y=node.attribute("y").as_float();shape.W=node.attribute("width").as_float();shape.H=node.attribute("height").as_float();shape.Radius=node.attribute("rx").as_float();}
      else if(name=="circle"){shape.Type=Kind::Circle;shape.X=node.attribute("cx").as_float();shape.Y=node.attribute("cy").as_float();shape.Radius=node.attribute("r").as_float();}
      else if(name=="line"){shape.Type=Kind::Line;shape.Points={{node.attribute("x1").as_float(),node.attribute("y1").as_float()},{node.attribute("x2").as_float(),node.attribute("y2").as_float()}};}
      else if(name=="polygon" || name=="polyline"){
        shape.Type=name=="polygon"?Kind::Polygon:Kind::Polyline;std::string points=node.attribute("points").value();std::replace(points.begin(),points.end(),',',' ');
        std::istringstream input(points);float px=0,py=0;while(input>>px>>py)shape.Points.push_back({px,py});
        if(shape.Points.size()<2)throw std::runtime_error("Invalid launcher SVG points.");
      }else throw std::runtime_error("Unsupported required SVG element: "+name);
      Shapes.push_back(std::move(shape));
    }
  }
  void Draw(ImVec2 pos,ImVec2 size)const{
    auto* draw=ImGui::GetWindowDrawList();const float scale=std::min(size.x/Width,size.y/Height);
    pos=Add(pos,{(size.x-Width*scale)/2,(size.y-Height*scale)/2});
    for(const auto& s:Shapes){const auto p=Add(pos,{s.X*scale,s.Y*scale});
      if(s.Type==Kind::Rect){const auto end=Add(p,{s.W*scale,s.H*scale});if(s.Fill)draw->AddRectFilled(p,end,s.Fill,s.Radius*scale);if(s.Stroke)draw->AddRect(p,end,s.Stroke,s.Radius*scale,s.Width*scale);}
      else if(s.Type==Kind::Circle){if(s.Fill)draw->AddCircleFilled(p,s.Radius*scale,s.Fill);if(s.Stroke)draw->AddCircle(p,s.Radius*scale,s.Stroke,0,s.Width*scale);}
      else{std::vector<ImVec2> points;for(const auto& v:s.Points)points.push_back(Add(pos,{v.x*scale,v.y*scale}));
        if(s.Type==Kind::Polygon && s.Fill)draw->AddConvexPolyFilled(points.data(),int(points.size()),s.Fill);
        if(s.Stroke)draw->AddPolyline(points.data(),int(points.size()),s.Stroke,s.Width*scale,s.Type==Kind::Polygon?ImDrawFlags_Closed:ImDrawFlags_None);}
    }
  }
};
struct LauncherWindow {
  SDL_Window* Window=nullptr;SDL_GLContext Context=nullptr;
  bool Imgui=false,Backend=false,Gl=false;
  float Scale=1;
  LauncherWindow(){
    Require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK),"Initialize launcher");
    try{
      Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3),"OpenGL version");Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3),"OpenGL version");
      Require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY),"OpenGL profile");
      Window=SDL_CreateWindow("Saekano - PC Launcher",1120,820,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);Require(Window!=nullptr,"Create launcher window");
      Scale=DisplayScale();SDL_Rect available{};SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(Window),&available);
      const int maxWidth=available.w>100?available.w-40:1920,maxHeight=available.h>100?available.h-60:1080;
      SDL_SetWindowMinimumSize(Window,std::min(int(850*Scale),maxWidth),std::min(int(620*Scale),maxHeight));
      SDL_SetWindowSize(Window,std::min(int(1120*Scale),maxWidth),std::min(int(820*Scale),maxHeight));SDL_SetWindowPosition(Window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
      Context=SDL_GL_CreateContext(Window);Require(Context!=nullptr,"Create launcher renderer");
      Require(gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))!=0,"Load OpenGL");SDL_GL_SetSwapInterval(1);
      IMGUI_CHECKVERSION();ImGui::CreateContext();Imgui=true;auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
#ifdef _WIN32
      const std::filesystem::path font="C:/Windows/Fonts/meiryo.ttc";
      if(std::filesystem::exists(font))io.Fonts->AddFontFromFileTTF(PcPathUtf8(font).c_str(),17);
#endif
      ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowPadding={20,16};style.FramePadding={10,7};style.ItemSpacing={10,9};style.FrameRounding=5;style.ChildRounding=6;
      style.Colors[ImGuiCol_WindowBg]={.065f,.085f,.12f,1};style.Colors[ImGuiCol_ChildBg]={.08f,.105f,.15f,1};style.Colors[ImGuiCol_Button]={.14f,.24f,.36f,1};style.Colors[ImGuiCol_ButtonHovered]={.2f,.37f,.53f,1};
      style.ScaleAllSizes(Scale);style.FontScaleDpi=Scale;
      Require(ImGui_ImplSDL3_InitForOpenGL(Window,Context),"Initialize launcher input");Backend=true;
      Require(ImGui_ImplOpenGL3_Init("#version 330"),"Initialize launcher drawing");Gl=true;
    }catch(...){Close();throw;}
  }
  void Close(){if(Gl){ImGui_ImplOpenGL3_Shutdown();Gl=false;}if(Backend){ImGui_ImplSDL3_Shutdown();Backend=false;}if(Imgui){ImGui::DestroyContext();Imgui=false;}if(Context){SDL_GL_DestroyContext(Context);Context=nullptr;}if(Window){SDL_DestroyWindow(Window);Window=nullptr;}SDL_Quit();}
  ~LauncherWindow(){Close();}
  float DisplayScale()const{const float density=SDL_GetWindowPixelDensity(Window),display=SDL_GetWindowDisplayScale(Window);return density>0 && display>0?display/density:1;}
  void Begin(){const auto next=DisplayScale();if(std::abs(next-Scale)>.01f){auto& style=ImGui::GetStyle();style.ScaleAllSizes(next/Scale);style.FontScaleDpi=next;Scale=next;}ImGui_ImplOpenGL3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();}
  void Present(){ImGui::Render();int w=0,h=0;SDL_GetWindowSizeInPixels(Window,&w,&h);glViewport(0,0,w,h);glClearColor(.065f,.085f,.12f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());SDL_GL_SwapWindow(Window);}
};
struct FolderResult {std::mutex Mutex;bool Pending=false,Ready=false;std::string Path,Error;};
void SDLCALL FolderChosen(void* userdata,const char* const* paths,int){
  std::unique_ptr<std::shared_ptr<FolderResult>> reference(static_cast<std::shared_ptr<FolderResult>*>(userdata));auto& state=**reference;
  std::lock_guard lock(state.Mutex);state.Pending=false;state.Ready=true;
  if(!paths)state.Error=SDL_GetError();else if(paths[0])state.Path=paths[0];
}
bool DeviceOverlap(const std::string& a,const std::string& b){return a.empty() || b.empty() || a==b || a.starts_with(b+"|") || b.starts_with(a+"|");}
bool Conflict(const PcBinding& a,const PcBinding& b){return a.Kind==b.Kind && a.Code==b.Code && a.Direction==b.Direction && DeviceOverlap(a.Device,b.Device);}
std::string ActionTitle(PcAction action){static const char* labels[]={"D-pad Up","D-pad Down","D-pad Left","D-pad Right","Circle / Confirm","Cross / Back","Square","Triangle","L","R","Start","Select","PC Skip"};return labels[size_t(action)];}
class Launcher {
 public:
  PcSettings Settings=PcSettings::Defaults();std::filesystem::path ConfigPath,SavePath;std::string Error,LoadError,PathText;
  bool Dirty=false,ReplaceInvalid=false,Play=false,Quit=false;
  LauncherWindow Host;PcInputRouter Input;PcDataValidator& Validator;
  std::map<std::string,Svg> Icons;
  std::optional<PcBindingCapture> Capture;std::optional<size_t> ReplaceBinding;
  bool OpenCapturePopup=false,CancelCapture=false;
  PcAction Selected=PcAction::Circle;
  std::string Device,ProfileName="My controls";bool ExactDevice=false;
  std::shared_ptr<FolderResult> Folder=std::make_shared<FolderResult>();
  ImVec2 CancelMin{},CancelMax{};bool CancelRect=false;
  Launcher(std::filesystem::path path,std::filesystem::path saves,const std::filesystem::path& usrdirOverride):ConfigPath(std::move(path)),SavePath(std::move(saves)),Input(Host.Window,Settings.InputProfile()),Validator(RuntimePcDataValidator()){
    try{Settings=LoadPcSettings(ConfigPath);}catch(const std::exception& e){LoadError=e.what();Error="Configuration could not be loaded. Defaults are shown; the existing file has not been changed.";}
    if(!usrdirOverride.empty()){Settings.Usrdir=std::filesystem::absolute(usrdirOverride);Dirty=true;}
    PathText=PcPathUtf8(Settings.Usrdir);Input.SetProfile(Settings.InputProfile());
    const auto assetRoot=PcPathFromUtf8(SDL_GetBasePath())/"saekano-launcher";
    for(size_t i=0;i<PcActionCount;++i)Icons.emplace(PcActionName(PcAction(i)),Svg(assetRoot/(std::string(PcActionName(PcAction(i)))+".svg")));
    Icons.emplace("vita",Svg(assetRoot/"vita.svg"));
    if(!Settings.Usrdir.empty())Validator.Start(Settings.Usrdir,Settings.StrictData);
  }
  template<class F>void Attempt(F&& f){try{f();}catch(const std::exception& e){Error=e.what();}}
  void Changed(){Dirty=true;}
  void RefreshInput(){Input.SetProfile(Settings.InputProfile());Changed();}
  void Save(){
    Settings.Validate();PcRequireOutsideData(ConfigPath,Settings.Usrdir);PcRequireOutsideData(SavePath,Settings.Usrdir);
    if(!LoadError.empty()){
      if(!ReplaceInvalid)throw std::runtime_error("Choose replacement of the invalid configuration explicitly before saving.");
      if(std::filesystem::exists(ConfigPath)){auto backup=ConfigPath;backup+=".invalid-"+std::to_string(SDL_GetTicksNS())+".bak";std::filesystem::copy_file(ConfigPath,backup);}
    }
    SavePcSettings(ConfigPath,Settings);LoadError.clear();ReplaceInvalid=false;Dirty=false;Error.clear();
  }
  void StartCapture(std::optional<size_t> replace={}){
    ReplaceBinding=replace;Capture.emplace(Selected,Input,Device);CancelRect=false;OpenCapturePopup=true;CancelCapture=false;
  }
  bool IconButton(const std::string& id,const Svg& icon,ImVec2 size,bool selected=false){
    const auto pos=ImGui::GetCursorScreenPos();const bool clicked=ImGui::InvisibleButton(id.c_str(),size);
    if(selected || ImGui::IsItemHovered())ImGui::GetWindowDrawList()->AddRectFilled(pos,Add(pos,size),selected?IM_COL32(48,90,131,255):IM_COL32(35,56,79,255),5);
    icon.Draw(Add(pos,{Units(3),Units(3)}),{size.x-Units(6),size.y-Units(6)});return clicked;
  }
  void Diagram(){
    const auto start=ImGui::GetCursorScreenPos();const float width=std::min(ImGui::GetContentRegionAvail().x,Units(540)),scale=width/640;
    Icons.at("vita").Draw(start,{width,280*scale});ImGui::Dummy({width,280*scale});const auto after=ImGui::GetCursorScreenPos();
    const std::array<ImVec2,12> positions={ImVec2{84,68},{84,132},{52,100},{116,100},{589,100},{556,133},{523,100},{556,67},{84,24},{556,24},{570,232},{530,232}};
    for(size_t i=0;i<positions.size();++i){const float size=std::max(Units(23),34*scale);ImGui::SetCursorScreenPos(Add(start,{positions[i].x*scale-size/2,positions[i].y*scale-size/2}));
      if(IconButton("diagram-"+std::to_string(i),Icons.at(PcActionName(PcAction(i))),{size,size},Selected==PcAction(i)))Selected=PcAction(i);
      if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",ActionTitle(PcAction(i)).c_str());}
    ImGui::SetCursorScreenPos(after);
  }
  void Profiles(){
    ImGui::SetNextItemWidth(Units(220));
    if(ImGui::BeginCombo("Profile",Settings.ActiveProfile.c_str())){
      for(const auto& p:Settings.Profiles)if(ImGui::Selectable(p.Name.c_str(),p.Name==Settings.ActiveProfile)){Settings.ActiveProfile=p.Name;RefreshInput();ReplaceBinding.reset();}
      ImGui::EndCombo();
    }
    ImGui::SameLine();ImGui::SetNextItemWidth(Units(190));ImGui::InputText("Name##profile",&ProfileName);
    const auto applyProfile=[&](bool duplicate){
      auto next=Settings;auto profile=duplicate?next.InputProfile():PcInputProfile{};profile.Name=ProfileName;next.Profiles.push_back(std::move(profile));next.ActiveProfile=ProfileName;next.Validate();Settings=std::move(next);RefreshInput();
    };
    if(ImGui::Button("New"))Attempt([&]{applyProfile(false);});ImGui::SameLine();if(ImGui::Button("Duplicate"))Attempt([&]{applyProfile(true);});ImGui::SameLine();
    ImGui::BeginDisabled(Settings.ActiveProfile=="Default");
    if(ImGui::Button("Rename"))Attempt([&]{auto next=Settings;next.InputProfile().Name=ProfileName;next.ActiveProfile=ProfileName;next.Validate();Settings=std::move(next);RefreshInput();});ImGui::SameLine();
    if(ImGui::Button("Delete")){std::erase_if(Settings.Profiles,[&](const auto& p){return p.Name==Settings.ActiveProfile;});Settings.ActiveProfile="Default";RefreshInput();}ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Reset profile")){auto defaults=PcSettings::Defaults().InputProfile();defaults.Name=Settings.ActiveProfile;Settings.InputProfile()=std::move(defaults);RefreshInput();}
  }
  void Devices(){
    std::string label="All devices / model-wide capture";
    for(const auto& [id,d]:Input.Devices()){(void)id;if(Device==d.Identity || Device==d.Guid)label=d.Name;}
    if(!Device.empty() && label=="All devices / model-wide capture")label="Disconnected: "+Device;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##device",label.c_str())){
      if(ImGui::Selectable("All devices / keyboard / mouse",Device.empty()))Device.clear();
      for(const auto& [id,d]:Input.Devices()){
        ImGui::PushID(int(id));if(ImGui::Selectable(d.Name.c_str(),Device==d.Identity || Device==d.Guid))Device=ExactDevice?d.Identity:d.Guid;
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",d.Guid.c_str(),d.Identity.c_str());ImGui::PopID();}
      ImGui::EndCombo();
    }
    if(ImGui::Checkbox("Bind selected individual device (otherwise its model)",&ExactDevice)){
      for(const auto& [id,d]:Input.Devices()){(void)id;if(Device==d.Identity || Device==d.Guid){Device=ExactDevice?d.Identity:d.Guid;break;}}
    }
    const auto selector=Device.empty()?std::string("*"):Device;auto& raw=Settings.InputProfile().RawDevices;
    bool isRaw=std::find(raw.begin(),raw.end(),selector)!=raw.end() || std::find(raw.begin(),raw.end(),"*")!=raw.end();
    if(ImGui::Checkbox("Use raw joystick buttons / axes / hats",&isRaw)){
      if(isRaw){if(std::find(raw.begin(),raw.end(),selector)==raw.end())raw.push_back(selector);}
      else{std::erase(raw,selector);std::erase(raw,"*");}RefreshInput();
    }
    ImGui::TextWrapped("Unmapped joysticks always use raw input. Standard and raw events from one controller are never counted twice.");
  }
  void Controls(){
    Profiles();ImGui::Separator();
    const float left=std::max(Units(340),ImGui::GetContentRegionAvail().x*.49f);
    ImGui::BeginChild("vita-controls",{left,0},ImGuiChildFlags_Borders);Diagram();
    ImGui::TextUnformatted("Choose a Vita control");
    if(ImGui::BeginTable("targets",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
      ImGui::TableSetupColumn("Vita",ImGuiTableColumnFlags_WidthFixed,Units(195));ImGui::TableSetupColumn("Assigned PC inputs");ImGui::TableHeadersRow();
      for(size_t i=0;i<PcActionCount;++i){ImGui::PushID(int(i));ImGui::TableNextRow();ImGui::TableNextColumn();
        if(IconButton("target",Icons.at(PcActionName(PcAction(i))),{Units(28),Units(28)},Selected==PcAction(i)))Selected=PcAction(i);
        ImGui::SameLine();if(ImGui::Selectable(ActionTitle(PcAction(i)).c_str(),Selected==PcAction(i),0,{0,Units(28)}))Selected=PcAction(i);
        ImGui::TableNextColumn();std::string names;for(const auto& b:Settings.InputProfile().Bindings)if(b.Action==PcAction(i)){if(!names.empty())names+="; ";names+=PcInputLabel(b);}
        ImGui::TextWrapped("%s",names.empty()?"Unbound":names.c_str());ImGui::PopID();}
      ImGui::EndTable();
    }
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("binding-editor",{0,0},ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Input device");Devices();ImGui::Separator();
    Icons.at(PcActionName(Selected)).Draw(ImGui::GetCursorScreenPos(),{Units(42),Units(42)});ImGui::Dummy({Units(42),Units(42)});ImGui::SameLine();ImGui::TextUnformatted(ActionTitle(Selected).c_str());
    if(ImGui::Button("Add binding",{-1,0}))StartCapture();
    auto& bindings=Settings.InputProfile().Bindings;std::optional<size_t> remove;
    for(size_t i=0;i<bindings.size();++i)if(bindings[i].Action==Selected){auto& b=bindings[i];ImGui::PushID(int(i));
      ImGui::Separator();ImGui::TextWrapped("%s",PcInputLabel(b).c_str());ImGui::TextDisabled("%s",b.Device.empty()?"Any device in this family":b.Device.c_str());
      if(ImGui::Button("Remap"))StartCapture(i);ImGui::SameLine();if(ImGui::Button("Remove"))remove=i;
      if((b.Kind==PcSourceKind::GamepadAxis || b.Kind==PcSourceKind::JoystickAxis) && ImGui::TreeNode("Axis calibration")){
        bool changed=ImGui::SliderFloat("Press threshold",&b.Threshold,.01f,1,"%.2f");changed|=ImGui::SliderFloat("Release dead zone",&b.Deadzone,0,.95f,"%.2f");changed|=ImGui::SliderFloat("Rest position",&b.Rest,-1,1,"%.2f");
        if(ImGui::Button("Invert direction")){b.Direction=-b.Direction;changed=true;}if(changed)RefreshInput();ImGui::TreePop();}
      ImGui::PopID();}
    if(remove){bindings.erase(bindings.begin()+ptrdiff_t(*remove));RefreshInput();}
    ImGui::Separator();ImGui::TextUnformatted("Live inputs");if(!Input.LastError.empty())ImGui::TextWrapped("%s",Input.LastError.c_str());
    bool any=false;for(const auto& s:Input.Snapshot()){
      if(!Device.empty() && Device!=s.Device && Device!=s.Guid)continue;
      const bool axis=s.Kind==PcSourceKind::GamepadAxis || s.Kind==PcSourceKind::JoystickAxis;
      if(!axis && s.Value==0)continue;any=true;PcBinding b;b.Kind=s.Kind;b.Code=s.Code;b.Direction=s.Value>=0?1:-1;
      ImGui::Text("%s: %.2f",PcInputLabel(b).c_str(),double(s.Value));if(axis)ImGui::ProgressBar((s.Value+1)/2,{-1,Units(5)},"");}
    if(!any)ImGui::TextDisabled("Operate a key, mouse button or connected controller.");
    ImGui::EndChild();
  }
  void CapturePopup(){
    if(!Capture)return;if(OpenCapturePopup){ImGui::OpenPopup("Bind Vita control");OpenCapturePopup=false;}ImGui::SetNextWindowSize({Units(530),0},ImGuiCond_Appearing);
    if(ImGui::BeginPopupModal("Bind Vita control",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
      if(CancelCapture){Capture.reset();CancelRect=false;CancelCapture=false;ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
      ImGui::TextUnformatted(ActionTitle(Selected).c_str());
      if(Capture->Candidate)ImGui::TextWrapped("Captured: %s",PcInputLabel(*Capture->Candidate).c_str());
      if(Capture->State==PcBindingCapture::Stage::Waiting)ImGui::TextWrapped("Press a key/button, move an axis or hat, or scroll. Keep analog controls at rest when starting capture. Escape can be bound like any other key; use Cancel to abandon capture.");
      if(Capture->State==PcBindingCapture::Stage::ReturnNeutral)ImGui::TextWrapped("Release the captured input or return it to its rest position before confirming.");
      if(Capture->State==PcBindingCapture::Stage::Ready){
        const auto& candidate=*Capture->Candidate;size_t conflicts=0;
        for(size_t i=0;i<Settings.InputProfile().Bindings.size();++i)if(i!=ReplaceBinding && Conflict(Settings.InputProfile().Bindings[i],candidate))++conflicts;
        if(conflicts)ImGui::TextWrapped("%zu overlapping binding(s). Replace them or explicitly keep both actions.",conflicts);
        const auto commit=[&](bool replaceConflicts){
          auto next=Settings;auto& bindings=next.InputProfile().Bindings;std::vector<PcBinding> kept;
          for(size_t i=0;i<bindings.size();++i)if(i!=ReplaceBinding && (!replaceConflicts || !Conflict(bindings[i],candidate)))kept.push_back(bindings[i]);
          kept.push_back(candidate);bindings=std::move(kept);next.Validate();Settings=std::move(next);RefreshInput();Capture.reset();CancelRect=false;ImGui::CloseCurrentPopup();
        };
        if(ImGui::Button(conflicts?"Replace conflicting bindings":"Use binding"))Attempt([&]{commit(true);});
        if(Capture && conflicts){ImGui::SameLine();if(ImGui::Button("Keep both"))Attempt([&]{commit(false);});}
      }
      if(Capture){if(ImGui::Button("Cancel")){Capture.reset();CancelRect=false;ImGui::CloseCurrentPopup();}else{CancelMin=ImGui::GetItemRectMin();CancelMax=ImGui::GetItemRectMax();CancelRect=true;}}
      ImGui::EndPopup();
    }
  }
  void Display(){
    ImGui::TextUnformatted("Initial game window");
    ImGui::SetNextItemWidth(Units(330));if(ImGui::BeginCombo("Resolution","Preset or custom dimensions")){
      for(const auto size:{std::pair(960,544),{1280,720},{1600,900},{1920,1080},{2560,1440}}){const auto name=std::to_string(size.first)+" x "+std::to_string(size.second);if(ImGui::Selectable(name.c_str())){Settings.Width=size.first;Settings.Height=size.second;Changed();}}
      ImGui::EndCombo();}
    ImGui::SetNextItemWidth(Units(160));if(ImGui::InputInt("Width",&Settings.Width))Changed();ImGui::SetNextItemWidth(Units(160));if(ImGui::InputInt("Height",&Settings.Height))Changed();
    ImGui::TextWrapped("The game keeps its logical layout and scales to the initial window size. You can resize the window while playing.");
    ImGui::Separator();ImGui::TextUnformatted("Startup");if(ImGui::Checkbox("Skip logos and go directly to the opening movie",&Settings.SkipLogos))Changed();
    ImGui::TextWrapped("This skips only the logos. The OP and required game initialization remain enabled.");
  }
  void GameData(){
    ImGui::TextUnformatted("Original game data (USRDIR)");ImGui::SetNextItemWidth(-Units(115));
    if(ImGui::InputText("##usrdir",&PathText)){Attempt([&]{Settings.Usrdir=PcPathFromUtf8(PathText);Changed();});}
    ImGui::SameLine();bool pending=false;{std::lock_guard lock(Folder->Mutex);pending=Folder->Pending;}
    ImGui::BeginDisabled(pending);
    if(ImGui::Button("Browse...")){
      {std::lock_guard lock(Folder->Mutex);Folder->Pending=true;Folder->Ready=false;Folder->Path.clear();Folder->Error.clear();}
      SDL_ShowOpenFolderDialog(FolderChosen,new std::shared_ptr<FolderResult>(Folder),Host.Window,PathText.empty()?nullptr:PathText.c_str(),false);
    }ImGui::EndDisabled();
    if(ImGui::Checkbox("Strict validation: check original SHA256 hashes",&Settings.StrictData))Changed();
    ImGui::TextWrapped("Normal mode checks the required filenames. Strict mode checks the supplied PCSG00543 reference revision; a different legitimate revision is reported as unverified, not silently accepted.");
    auto state=Validator.Status();ImGui::BeginDisabled(state.Running || pending);
    if(ImGui::Button("Validate data"))Attempt([&]{Validator.Start(Settings.Usrdir,Settings.StrictData);});ImGui::EndDisabled();
    if(state.Running){ImGui::SameLine();if(ImGui::Button("Cancel validation"))Validator.Cancel();}
    ImGui::TextWrapped("%s",state.Message.empty()?"Select USRDIR, then validate it before Play.":state.Message.c_str());
    if(state.Running){const auto fraction=state.Strict?float(state.FinishedBytes)/float(std::max(uint64_t(1),state.TotalBytes)):float(state.FinishedFiles)/float(std::max(size_t(1),state.TotalFiles));ImGui::ProgressBar(fraction,{-1,0});ImGui::Text("%zu / %zu files - %s",state.FinishedFiles,state.TotalFiles,state.Current.c_str());}
    if(!state.Running && state.Passed && (state.Usrdir!=Settings.Usrdir || state.Strict!=Settings.StrictData))ImGui::TextWrapped("The selected path or validation mode changed. Validate again.");
    if(state.Passed && state.Strict)ImGui::TextDisabled("File size/time is checked again on Play; Validate data recomputes all hashes.");
    for(const auto& error:state.Errors)ImGui::BulletText("%s",error.c_str());
    ImGui::Separator();ImGui::TextWrapped("Original files stay read-only. Configuration and PC saves are stored separately. No data is extracted or modified by the launcher.");
  }
  void Events(){
    SDL_Event e{};while(SDL_PollEvent(&e)){
      if(e.type==SDL_EVENT_QUIT)Quit=true;
      if(Capture && (e.type==SDL_EVENT_JOYSTICK_REMOVED || e.type==SDL_EVENT_GAMEPAD_REMOVED || e.type==SDL_EVENT_WINDOW_FOCUS_LOST)){CancelCapture=true;Error="Device removed or window focus lost. Binding capture cancelled.";}
      for(const auto& sample:Input.Inspect(e))if(Capture){
        const bool cancelClick=sample.Kind==PcSourceKind::MouseButton && e.button.x>=CancelMin.x && e.button.x<=CancelMax.x && e.button.y>=CancelMin.y && e.button.y<=CancelMax.y && CancelRect;
        if(!cancelClick)Capture->Observe(sample);
      }
      ImGui_ImplSDL3_ProcessEvent(&e);
    }
    std::lock_guard lock(Folder->Mutex);
    if(Folder->Ready){Folder->Ready=false;if(!Folder->Error.empty())Error=Folder->Error;
      else if(!Folder->Path.empty()){PathText=Folder->Path;Attempt([&]{Settings.Usrdir=PcPathFromUtf8(PathText);Changed();if(!Validator.Status().Running)Validator.Start(Settings.Usrdir,Settings.StrictData);});}}
  }
  bool Run(){
    while(true){Events();bool pending=false;{std::lock_guard lock(Folder->Mutex);pending=Folder->Pending;}
      if(Quit && !pending)break;
      auto& io=ImGui::GetIO();if(Capture)io.ConfigFlags&=~ImGuiConfigFlags_NavEnableKeyboard;else io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
      Host.Begin();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("Saekano launcher",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
      ImGui::TextUnformatted("SAEKANO  |  PC LAUNCHER");ImGui::TextDisabled("Display, controller configuration and original game data");ImGui::Separator();
      ImGui::BeginDisabled(Capture.has_value() || pending);
      ImGui::BeginChild("settings-body",{0,-Units(115+(!LoadError.empty()?80.f:0.f)+(!Error.empty()?35.f:0.f))});
      if(ImGui::BeginTabBar("settings")){
        if(ImGui::BeginTabItem("Display")){Display();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Controls")){Controls();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Game Data")){GameData();ImGui::EndTabItem();}ImGui::EndTabBar();}
      ImGui::EndChild();ImGui::Separator();
      if(!LoadError.empty()){ImGui::TextWrapped("%s",LoadError.c_str());ImGui::Checkbox("Replace invalid configuration (keep a backup first)",&ReplaceInvalid);}
      if(!Error.empty()){ImGui::PushStyleColor(ImGuiCol_Text,{1,.62f,.48f,1});ImGui::TextWrapped("%s",Error.c_str());ImGui::PopStyleColor();}
      if(ImGui::Button("Save settings",{Units(150),0}))Attempt([&]{Save();});ImGui::SameLine();
      const auto validation=Validator.Status();const bool ready=validation.Passed && !validation.Running && validation.Usrdir==Settings.Usrdir && validation.Strict==Settings.StrictData;
      ImGui::BeginDisabled(!ready);
      if(ImGui::Button("Save and Play",{Units(175),0}))Attempt([&]{Save();if(!Validator.Fresh(Settings.Usrdir,Settings.StrictData)){Validator.Start(Settings.Usrdir,Settings.StrictData);throw std::runtime_error("Data changed. Validation is running again before Play.");}Play=true;});
      ImGui::EndDisabled();ImGui::SameLine();ImGui::TextUnformatted(Dirty?"Unsaved changes":ready?"Ready":"Validate data in Game Data before Play");
      ImGui::TextDisabled("Config: %s",PcPathUtf8(ConfigPath).c_str());ImGui::TextDisabled("Saves: %s",PcPathUtf8(SavePath).c_str());ImGui::EndDisabled();CapturePopup();ImGui::End();Host.Present();
      if(Play)break;
    }
    if(Validator.Status().Running)Validator.Cancel();return Play;
  }
};
}
int RunPcLauncher(const std::filesystem::path& requested,const std::filesystem::path& usrdirOverride,const std::filesystem::path& saveOverride){
  try{
    const auto path=requested.empty()?DefaultPcConfigPath():std::filesystem::absolute(requested);
    const auto saves=saveOverride.empty()?path.parent_path()/"saves":std::filesystem::absolute(saveOverride);
    PcSettings settings;bool play=false;
    {Launcher launcher(path,saves,usrdirOverride);play=launcher.Run();if(play)settings=launcher.Settings;}
    if(!play)return 0;SetRuntimePcSettings(settings);return RunTitleScene(settings.Usrdir,saves);
  }catch(const std::exception& e){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Saekano launcher",e.what(),nullptr);return 1;}
}
} // namespace Impacto::Saekano
