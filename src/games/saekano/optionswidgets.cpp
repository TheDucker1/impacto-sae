#include "optionswidgets.h"
#include "commands.h"
#include <bit>
namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
OptionsWidgetId Normalize(OptionsWidgetId id){if(id.Kind==OptionsWidgetKind::Background)id.Page=-1;return id;}
bool Equal(OptionsWidgetId a,OptionsWidgetId b){return a.Page==b.Page && a.Kind==b.Kind && a.Index==b.Index;}
UiSpriteDefinition Rectangle(std::array<int32_t,4> r,int32_t alignment=0){UiSpriteDefinition s;s.Alignment=alignment;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;}
}
OptionsWidgets::OptionsWidgets(const OptionsEnvironment& e,const OptionsValues& values,bool alternate,uint32_t hold){
  const auto rect=[&](std::array<int32_t,4> r,int32_t alignment=0){return OptionsWidgetComponent(true,Rectangle(r,alignment),{ProfileTextureSection::SharedUi,-1,0},true,hold);};
  const auto add=[&](OptionsWidgetId id,OptionsWidgetComponent component){OptionsWidgetGroup g;g.Variants.push_back(std::move(component));records_.push_back({id,{}});records_.back().Groups.push_back(std::move(g));};
  const auto pair=[&](OptionsWidgetId id,std::array<int32_t,4> a,std::array<int32_t,4> b){OptionsWidgetGroup g;g.Variants.push_back(rect(a));g.Variants.push_back(rect(b));records_.push_back({id,{}});records_.back().Groups.push_back(std::move(g));};
  add({-1,OptionsWidgetKind::Background,0},rect({0,0,960,544}));
  const auto& label=e.Sprites.at(0x3f4);
  for(uint32_t i=0;i<7;++i){const auto p=e.PointTables.at(0x15c).at(i);add({0,OptionsWidgetKind::Label,i},rect({p[0],p[1],label.Width,label.Height}));}
  const auto toggle=e.Rectangles.at(0x534);
  constexpr std::array<size_t,4> settings{2,4,5,6};
  for(uint32_t i=0;i<4;++i){const auto& p=e.PointRows.at(0x13c).at(i);const auto y=e.PointTables.at(0x15c).at(i+3)[1];
    pair({0,OptionsWidgetKind::Toggle,i},{Add(p.at(0)[0],toggle[0]),y,toggle[2],toggle[3]},
         {Add(p.at(1)[0],toggle[0]),y,toggle[2],toggle[3]});SetToggleVariant(0,i,!values[settings[i]]);}
  for(uint32_t i=0;i<3;++i)add({0,OptionsWidgetKind::Slider,i},rect(e.Rectangles.at(0x494)));
  for(uint32_t i=0;i<6;++i){const auto p=e.PointTables.at(0x1e0).at(i);add({1,OptionsWidgetKind::Label,i},rect({p[0],p[1],label.Width,label.Height}));}
  // Native uses the first TEXT toggle's X pair for the audio contact variants.
  // Its visible checkmark uses the separate audio table in the page renderer.
  const auto& audio=e.PointRows.at(0x13c).at(0);const auto audioY=e.PointTables.at(0x1e0).at(5)[1];
  pair({1,OptionsWidgetKind::Toggle,0},{Add(audio.at(0)[0],toggle[0]),audioY,toggle[2],toggle[3]},
       {Add(audio.at(1)[0],toggle[0]),audioY,toggle[2],toggle[3]});SetToggleVariant(1,0,!values[12]);
  for(uint32_t i=0;i<5;++i)add({1,OptionsWidgetKind::Slider,i},rect(e.Rectangles.at(0x494)));
  for(uint32_t i=0;i<2;++i){const auto& s=e.SpriteTables.at(0x240).at(i);add({2,OptionsWidgetKind::GroupLabel,i},rect({s.X,s.Y,s.Width,s.Height}));}
  const auto& cursor=e.Sprites.at(0x37c);
  for(uint32_t i=0;i<9;++i)add({2,OptionsWidgetKind::CharacterLabel,i},rect({e.IntegerTables.at(0x3a0).at(i%3),e.IntegerTables.at(0x3b0).at(i/3),cursor.Width,cursor.Height}));
  const auto character=e.Rectangles.at(0x544);const auto shift=std::bit_cast<int32_t>(e.Scalars.at(0x3d0));
  for(uint32_t i=0;i<9;++i){const auto x=Add(e.IntegerTables.at(0x3c0).at(i%3),character[0]),y=e.IntegerTables.at(0x3d4).at(i/3);
    pair({2,OptionsWidgetKind::CharacterToggle,i},{x,y,character[2],character[3]},
         {Add(x,shift),y,character[2],character[3]});SetToggleVariant(2,i,!values[14+i]);}
  auto revert=e.Sprites.at(0x500);revert.Color={255,255,255,255};
  add({-1,OptionsWidgetKind::Revert,0},OptionsWidgetComponent(false,revert,{ProfileTextureSection::SharedUi,-1,2},false,hold));
  OptionsWidgetGroup contact;contact.Variants.push_back(rect(e.Rectangles.at(0x524)));records_.back().Groups.push_back(std::move(contact));
  const auto& tabs=e.RectangleRows.at(0x108).at(alternate?1:0);
  for(uint32_t i=0;i<(alternate?4u:3u);++i)add({-1,OptionsWidgetKind::Tab,i},rect(tabs.at(i)));
  for(uint32_t i=0;i<5;++i){const auto& s=e.Sprites.at(uint16_t(0x274+i*36));add({2,OptionsWidgetKind::Mode,i},rect({s.X,s.Y,s.Width,s.Height},s.Alignment));}
}
OptionsWidgetRecord& OptionsWidgets::Find(OptionsWidgetId id){id=Normalize(id);for(auto& r:records_)if(Equal(r.Id,id))return r;throw FormatError("unknown Options widget");}
const OptionsWidgetRecord& OptionsWidgets::Find(OptionsWidgetId id)const{id=Normalize(id);for(const auto& r:records_)if(Equal(r.Id,id))return r;throw FormatError("unknown Options widget");}
void OptionsWidgets::Update(OptionsWidgetId id,int32_t x,int32_t y,uint64_t now){
  for(auto& g:Find(id).Groups)for(auto& c:g.Variants){
    auto& b=c.Body;const auto event=SampleWidgetContact(b.HitRectangle({x,y}),input_.Current[0],input_.Previous[0],b.Input.TrackedId,input_.Velocity(0));
    b.Update({x,y},event,now);input_.Publish(b.Input.Flags);
  }
}
OptionsWidgetContact OptionsWidgets::Contact(OptionsWidgetId id)const{
  OptionsWidgetContact out;for(const auto& g:Find(id).Groups){const auto& h=g.Variants.at(g.Selected).Body.Input;
    if(h.Enabled){out.Pressed|=(h.Flags&1)!=0;out.Held|=(h.Flags&2)!=0;out.Released|=(h.Flags&8)!=0;}}
  if(input_.Current[0])out.PointerX=input_.Current[0]->Position[0];return out;
}
void OptionsWidgets::SetToggleVariant(uint32_t page,uint32_t index,bool inverted){
  // Shared9bc06/9c2ba select variant1 when condition flag0, variant0 when1.
  if(page>2)throw FormatError("invalid Options toggle page");Find({int32_t(page),page==2?OptionsWidgetKind::CharacterToggle:OptionsWidgetKind::Toggle,index}).Groups.at(0).Selected=size_t(!inverted);
}
std::vector<UiRenderCommand> OptionsWidgets::Draw(OptionsWidgetId id,std::array<uint8_t,4> tint)const{
  std::vector<UiRenderCommand> out;for(const auto& g:Find(id).Groups){const auto& c=g.Variants.at(g.Selected);UiRenderCommand draw;
    if(c.Rectangle){draw.Kind=UiRenderKind::Rectangle;draw.Rectangle=c.Body.HitRectangle(c.Body.Offset);
      for(size_t i=0;i<4;++i)draw.Color[i]=uint8_t(uint32_t(float(uint32_t(c.Body.Shape.Color[i])*tint[i])/255.0f));}
    else{draw.Kind=UiRenderKind::Sprite;draw.Sprite=c.Body.Draw(tint);}out.push_back(draw);}
  return out;
}
void OptionsWidgets::CancelPointer(){input_.Current=input_.Previous={};for(auto& r:records_)for(auto& g:r.Groups)for(auto& c:g.Variants){c.Body.Input.TrackedId=-1;c.Body.Input.Flags=0;}}
}
