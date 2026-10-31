#include "optionspagedraw.h"
#include "commands.h"
#include <bit>
#include <cmath>
namespace Impacto::Saekano {
namespace {
int32_t Subtract(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t Truncate(float v){if(std::isnan(v))return 0;if(v>=2147483648.0f)return INT32_MAX;if(v<=-2147483648.0f)return INT32_MIN;return int32_t(v);}
}
OptionsPageDrawInputs OptionsPageDrawFromState(const OptionsOwnerState& s,uint8_t alpha){
  if(!s.Started)throw UnsupportedCommand("Options page draw has no owner");
  OptionsPageDrawInputs in;in.Page=uint32_t(s.Pages.Controls.SelectedTab);in.Values=s.Pages.Values;
  for(size_t i=0;i<3;++i){in.SelectedRows[i]=s.Pages.Rows[i].Selected;in.TextX[i]=s.Pages.Text.Sliders[i].X;}
  for(size_t i=0;i<5;++i)in.AudioX[i]=s.Pages.Audio.Sliders[i].X;
  in.Character=s.Pages.Voice.Character;in.HighlightProgress=s.Pages.Controls.SampleProgress.Value.Current;
  in.HighlightAlpha=alpha;return in;
}
std::vector<OptionsPageDrawCommand> BuildOptionsPageDraw(const OptionsEnvironment& e,
    const OptionsPageDrawInputs& in,const GsfFont& font,uint32_t utility,uint32_t nominal){
  if(in.Page>2 || in.SelectedRows[in.Page]<0 || in.SelectedRows[in.Page]>(in.Page==0?6:in.Page==1?5:1))
    throw FormatError("invalid Options page draw selection");
  std::vector<OptionsPageDrawCommand> out;
  const auto widget=[&](OptionsWidgetKind kind,uint32_t index){OptionsPageDrawCommand c;c.Kind=OptionsPageDrawKind::Widget;c.Widget={int32_t(in.Page),kind,index};out.push_back(c);};
  const auto sprite=[&](uint32_t texture,UiSpriteDefinition s){OptionsPageDrawCommand c;
    c.Sprite={{ProfileTextureSection::SystemUi,-1,texture},{s.X,s.Y,Truncate(float(s.Width)),Truncate(float(s.Height))},
      {s.SourceX,s.SourceY,s.Width,s.Height},s.Color};out.push_back(c);};
  const auto positioned=[&](uint32_t texture,UiSpriteDefinition s,OptionsEnvironment::Point point){s.X=point[0];s.Y=point[1];sprite(texture,s);};
  const auto text=[&](const std::u16string& value){
    const auto scale=std::bit_cast<float>(e.Scalars.at(0x4ac));const auto product=float(utility)*scale;
    if(!std::isfinite(product) || product<0 || product>=2147483648.0f)throw FormatError("invalid Options help font scale");
    const auto size=uint32_t(Truncate(product));const auto position=e.Points.at(0x4b0);
    OptionsPageDrawCommand c;c.Kind=OptionsPageDrawKind::Text;
    c.Text={value,{Subtract(position[0],std::bit_cast<int32_t>(MeasurePvfText(font,value,size,nominal))),position[1]},size,{0,0,0,255}};out.push_back(c);
  };
  const auto& values=in.Values;const auto row=size_t(in.SelectedRows[in.Page]);
  if(in.Page<2){
    const bool audio=in.Page==1;const size_t labels=audio?6:7,numeric=audio?5:3;
    for(size_t i=0;i<labels;++i)widget(OptionsWidgetKind::Label,uint32_t(i));
    auto highlight=e.Sprites.at(0x3f4);highlight.Color[3]=in.HighlightAlpha;
    positioned(27,highlight,e.PointTables.at(audio?0x1e0:0x15c).at(row));
    sprite(audio?35:38,e.Sprites.at(audio?0x19c:0x118));
    if(!audio)for(uint32_t i=0;i<4;++i)widget(OptionsWidgetKind::Toggle,i);
    for(size_t i=0;i<numeric;++i)widget(OptionsWidgetKind::Slider,uint32_t(i));
    if(audio)widget(OptionsWidgetKind::Toggle,0);
    for(size_t i=0;i<numeric;++i){
      const auto shape=audio?e.Sprites.at(0x460):i<2?e.SpriteTables.at(0x484).at(values[i]):e.Sprites.at(0x43c);
      auto handle=shape;handle.X=audio?in.AudioX[i]:in.TextX[i];
      handle.Y=e.Points.at(uint16_t((audio?0x1f0:0x16c)+i*16))[1];sprite(30,handle);
    }
    size_t description=0;
    const std::array<size_t,4> toggles{2,4,5,6};
    const size_t toggleCount=audio?1:4;
    for(size_t i=0;i<toggleCount;++i){
      const size_t index=values[audio?12:toggles[i]]?0:1;
      positioned(26,e.Sprites.at(0x418),audio?e.PointRows.at(0x1c0).at(0).at(index):e.PointRows.at(0x13c).at(i).at(index));
      if(row==numeric+i)description=index;
    }
    text(e.StringRows.at(audio?0x1d0:0x14c).at(row).at(description));
  }else{
    if(in.Character<0 || in.Character>=9 || values[13]>=5 || in.HighlightProgress<0 || in.HighlightProgress>8)
      throw FormatError("invalid Options voice draw selector");
    for(uint32_t i=0;i<2;++i)widget(OptionsWidgetKind::GroupLabel,i);
    auto highlight=e.SpriteTables.at(0x240).at(row);highlight.Color[3]=in.HighlightAlpha;
    sprite(row?29:28,highlight);sprite(row?58:59,e.Sprites.at(0x250));sprite(row?48:47,e.Sprites.at(0x334));
    constexpr std::array<uint32_t,5> textures{54,49,60,56,52};
    for(uint32_t i=0;i<5;++i){
      widget(OptionsWidgetKind::Mode,i);auto shape=e.Sprites.at(uint16_t(0x274+i*36));
      const bool selected=i==values[13];int32_t offset=0;float scale=1;
      if(selected){
        const auto p=in.HighlightProgress;offset=p-(p/4*(p%4)*2);
        float growth=std::bit_cast<float>(e.Scalars.at(0x330))*float(offset);
        growth=growth/10.0f;scale=1.0f+growth;shape.Y=Subtract(shape.Y,offset);
      }
      OptionsPageDrawCommand c;c.Sprite=ResolveUiSprite({ProfileTextureSection::SystemUi,-1,textures[i]+uint32_t(selected)},shape,scale,scale);out.push_back(c);
    }
    sprite(46,e.Sprites.at(0x358));
    for(uint32_t i=0;i<9;++i)widget(OptionsWidgetKind::CharacterToggle,i);
    for(uint32_t i=0;i<9;++i)widget(OptionsWidgetKind::CharacterLabel,i);
    for(size_t i=0;i<9;++i){
      auto point=OptionsEnvironment::Point{e.IntegerTables.at(0x3c0).at(i%3),e.IntegerTables.at(0x3d4).at(i/3)};
      if(!values[14+i])point[0]=std::bit_cast<int32_t>(uint32_t(point[0])+e.Scalars.at(0x3d0));
      positioned(26,e.Sprites.at(0x418),point);
    }
    positioned(51,e.Sprites.at(0x37c),{e.IntegerTables.at(0x3a0).at(size_t(in.Character)%3),e.IntegerTables.at(0x3b0).at(size_t(in.Character)/3)});
    const size_t index=row?size_t(in.Character):0;const size_t description=row?size_t(!values[14+index]):values[13];
    text(e.StringGrids.at(0x3e4).at(row).at(index).at(description));
  }
  return out;
}
}
