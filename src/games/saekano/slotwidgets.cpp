#include "slotwidgets.h"
#include "commands.h"
#include <bit>
namespace Impacto::Saekano {
namespace {int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}}
SlotWidgets::SlotWidgets(const SlotEnvironment& e,uint32_t hold){
  const ProfileTextureKey unused{ProfileTextureSection::SharedUi,-1,0};
  const auto rect=[&](SlotEnvironment::Rectangle r){UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return SlotWidgetComponent(true,s,unused,true,hold);};
  const auto add=[&](SlotBrowserWidget kind,uint32_t row,SlotWidgetComponent component){records_.push_back({kind,row,{}});records_.back().Components.push_back(std::move(component));};
  auto back=e.Sprites.at(0xa0);back.Color={255,255,255,255};
  add(SlotBrowserWidget::Back,0,SlotWidgetComponent(false,back,{ProfileTextureSection::SharedUi,-1,2},false,hold));
  records_.back().Components.push_back(rect(e.Rectangles.at(0xc4)));
  const auto& body=e.Sprites.at(0x168);const auto protection=e.Rectangles.at(0x31c);
  for(uint32_t i=0;i<4;++i){
    const auto y=Add(std::bit_cast<int32_t>(e.Scalars.at(0x160)),std::bit_cast<int32_t>((uint32_t(body.Height)+e.Scalars.at(0x164))*i));
    add(SlotBrowserWidget::Activate,i,rect({body.X,Add(y,body.Y),body.Width,body.Height}));
    add(SlotBrowserWidget::Protect,i,rect({Add(body.X,protection[0]),Add(y,protection[1]),protection[2],protection[3]}));
  }
  add(SlotBrowserWidget::Page,0,rect(e.Rectangles.at(0x148)));
}
SlotWidgetRecord& SlotWidgets::Find(SlotBrowserWidget k,uint32_t row){for(auto& r:records_)if(r.Kind==k && r.Row==row)return r;throw FormatError("unknown slot widget");}
const SlotWidgetRecord& SlotWidgets::Find(SlotBrowserWidget k,uint32_t row)const{for(const auto& r:records_)if(r.Kind==k && r.Row==row)return r;throw FormatError("unknown slot widget");}
void SlotWidgets::Update(SlotBrowserWidget k,uint32_t row,uint64_t now){
  for(auto& c:Find(k,row).Components){auto& b=c.Body;const auto event=SampleWidgetContact(b.HitRectangle({0,0}),input_.Current[0],input_.Previous[0],b.Input.TrackedId,input_.Velocity(0));b.Update({0,0},event,now);input_.Publish(b.Input.Flags);}
}
bool SlotWidgets::Released(SlotBrowserWidget k,uint32_t row)const{for(const auto& c:Find(k,row).Components)if(c.Body.Input.Enabled && (c.Body.Input.Flags&8)!=0)return true;return false;}
bool SlotWidgets::PageFlick()const{for(const auto& c:Find(SlotBrowserWidget::Page,0).Components)if(c.Body.Input.Enabled && (c.Body.Input.Flags&0x80)!=0)return true;return false;}
float SlotWidgets::PageVelocityY()const{
  const auto& input=Find(SlotBrowserWidget::Page,0).Components.back().Body.Input;
  if(!input.AbsoluteVelocity)throw UnsupportedCommand("slot page velocity is unknown before release");return input.Velocity[1];
}
std::vector<UiRenderCommand> SlotWidgets::Draw(SlotBrowserWidget k,uint32_t row,std::array<uint8_t,4> tint)const{
  std::vector<UiRenderCommand> out;
  for(const auto& c:Find(k,row).Components){UiRenderCommand command;
    if(c.Rectangle){command.Kind=UiRenderKind::Rectangle;command.Rectangle=c.Body.HitRectangle(c.Body.Offset);for(size_t i=0;i<4;++i)command.Color[i]=uint8_t(uint32_t(float(uint32_t(c.Body.Shape.Color[i])*tint[i])/255.f));}
    else{command.Kind=UiRenderKind::Sprite;command.Sprite=c.Body.Draw(tint);}out.push_back(command);
  }
  return out;
}
void SlotWidgets::CancelPointer(){input_.Current=input_.Previous={};for(auto& r:records_)for(auto& c:r.Components){c.Body.Input.Flags=0;c.Body.Input.TrackedId=-1;}}
SlotWidgetServices::SlotWidgetServices(SlotWidgets& w,std::function<void(int32_t)> sound):widgets_(w),sound_(std::move(sound)){if(!sound_)throw FormatError("slot UI sound service is missing");}
void SlotWidgetServices::BeginFrame(WidgetInputHistory::Snapshot snapshot,uint64_t now){now_=now;widgets_.BeginFrame(std::move(snapshot));}
void SlotWidgetServices::UpdateWidget(SlotBrowserWidget k,uint32_t row){widgets_.Update(k,row,now_);}
bool SlotWidgetServices::PageFlick(){return widgets_.PageFlick();}
float SlotWidgetServices::PageVelocityY(){return widgets_.PageVelocityY();}
bool SlotWidgetServices::Released(SlotBrowserWidget k,uint32_t row){return widgets_.Released(k,row);}
void SlotWidgetServices::UiSound(int32_t id){sound_(id);}
}
