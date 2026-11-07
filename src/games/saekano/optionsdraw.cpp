#include "optionsdraw.h"
#include "commands.h"
#include <bit>
#include <cmath>
#include <limits>
namespace Impacto::Saekano {
namespace {
int32_t Subtract(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t Truncate(float value){
  if(std::isnan(value))return 0;
  if(value>=2147483648.0f)return INT32_MAX;
  if(value<=-2147483648.0f)return INT32_MIN;
  return int32_t(value);
}
int32_t Anchor(int32_t value,int32_t extent,int32_t alignment){
  return alignment==1?Subtract(value,extent/2):alignment==2?Subtract(value,extent):value;
}
}
OptionsDrawInputs OptionsDrawFromState(const OptionsOwnerState& s,bool alternate,bool question,
    std::array<int32_t,2> position){
  if(!s.Started)throw UnsupportedCommand("Options draw has no owner");
  OptionsDrawInputs in;in.AlternateHeader=alternate;in.QuestionPresent=question;
  in.SelectedTab=s.Pages.Controls.SelectedTab;in.TabCount=s.Pages.Controls.TabCount;
  in.ArrowActive=s.Pages.Controls.ArrowActive;in.FadeAlpha=s.Lifecycle.Fade.Value.Current;
  for(size_t i=0;i<2;++i)in.ArrowOffset[i]=s.Pages.Controls.Arrows[i].Current;
  in.DecorationPosition=position;return in;
}
std::vector<OptionsDrawCommand> BuildOptionsDraw(const OptionsEnvironment& e,const OptionsDrawInputs& in){
  if(in.TabCount<3 || in.TabCount>4 || in.SelectedTab<0 || in.SelectedTab>=in.TabCount)
    throw FormatError("invalid Options draw tab");
  for(auto offset:in.ArrowOffset)if(!std::isfinite(offset))throw FormatError("nonfinite Options arrow offset");
  std::vector<OptionsDrawCommand> out;
  const auto operation=[&](OptionsDrawKind kind,uint32_t index=0){OptionsDrawCommand c;c.Kind=kind;c.Index=index;out.push_back(c);};
  const auto emit=[&](ProfileTextureSection section,uint32_t texture,UiSpriteDefinition s,bool aligned=false,
                      std::optional<OptionsEnvironment::Point> extent=std::nullopt){
    const auto size=extent.value_or(OptionsEnvironment::Point{Truncate(float(s.Width)),Truncate(float(s.Height))});
    const auto x=aligned?Anchor(s.X,size[0],s.Alignment%3):s.X;
    const auto y=aligned?Anchor(s.Y,size[1],s.Alignment/3):s.Y;
    OptionsDrawCommand c;c.Sprite={{section,-1,texture},{x,y,size[0],size[1]},
      {s.SourceX,s.SourceY,s.Width,s.Height},s.Color};out.push_back(c);
  };
  const auto system=[&](uint32_t texture,UiSpriteDefinition s){emit(ProfileTextureSection::SystemUi,texture,s);};
  system(25,e.Sprites.at(0x24));system(36,e.Sprites.at(0x48));
  const size_t variant=in.AlternateHeader?1:0;
  auto sprite=e.Sprites.at(0x6c);const auto position=e.PointTables.at(0x90).at(variant);
  sprite.X=position[0];sprite.Y=position[1];system(37,sprite);
  emit(ProfileTextureSection::SharedUi,17,e.Sprites.at(0x578),true,e.Points.at(0x59c));
  emit(ProfileTextureSection::SharedUi,17,e.Sprites.at(0x554),true);
  for(int32_t i=0;i<in.TabCount;++i)operation(OptionsDrawKind::Widget,uint32_t(i));
  // Both header variants use45 for the fourth tab. Initialize it explicitly;
  // native only initializes this slot in its alternate-header branch.
  const std::array<uint32_t,4> textures=in.AlternateHeader?
    std::array<uint32_t,4>{39,41,43,45}:std::array<uint32_t,4>{40,42,44,45};
  system(textures[size_t(in.SelectedTab)],e.SpriteTables.at(0xf8).at(variant));
  sprite=e.Sprites.at(0xa0);const auto left=e.PointTables.at(0xc4).at(variant);
  sprite.X=Truncate(float(left[0])-in.ArrowOffset[0]);sprite.Y=left[1];
  system(in.ArrowActive[0]?32:31,sprite);
  sprite=e.Sprites.at(0xd4);sprite.X=Truncate(float(sprite.X)+in.ArrowOffset[1]);
  system(in.ArrowActive[1]?34:33,sprite);
  emit(ProfileTextureSection::SharedUi,8,e.Sprites.at(0x4b8));
  emit(ProfileTextureSection::SharedUi,6,e.Sprites.at(0x4dc));
  operation(OptionsDrawKind::Widget,4);
  operation(OptionsDrawKind::Decoration);out.back().Rectangle={in.DecorationPosition[0],in.DecorationPosition[1],0,0};
  if(in.SelectedTab<3)operation(OptionsDrawKind::Page,uint32_t(in.SelectedTab));
  if(in.QuestionPresent){operation(OptionsDrawKind::Question);out.back().Rectangle={480,272,0,0};out.back().Color={0,0,0,90};}
  operation(OptionsDrawKind::Save);out.back().Color={0,0,0,90};
  operation(OptionsDrawKind::Overlay);out.back().Rectangle={0,0,960,544};out.back().Color={255,255,255,uint8_t(in.FadeAlpha)};
  return out;
}
}
