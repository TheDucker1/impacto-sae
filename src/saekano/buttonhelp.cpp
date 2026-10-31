#include "buttonhelp.h"
#include "commands.h"
#include <bit>
#include <cmath>
namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t Sub(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
int32_t Integer(float v){if(std::isnan(v))return 0;if(v>=2147483648.0f)return INT32_MAX;if(v<=-2147483648.0f)return INT32_MIN;return int32_t(v);}
template<class Read>auto Table(PayloadReader& r,size_t stride,Read read){const auto n=r.U32();r.CheckCount(n,stride);std::vector<decltype(read())> out;out.reserve(n);for(uint32_t i=0;i<n;++i)out.push_back(read());return out;}
}
ButtonHelpEnvironment ButtonHelpEnvironment::Decode(std::span<const uint8_t> bytes){
  PayloadReader r(bytes);ButtonHelpEnvironment e;for(auto& v:e.Flags)v=r.U8();for(auto& v:e.Viewport)v=r.I32();
  const auto rectangle=[&]{return std::array<int32_t,4>{r.I32(),r.I32(),r.I32(),r.I32()};};
  const auto item=[&]{ButtonHelpItem v;v.Key=r.I32();v.Text=r.String();return v;};
  const auto items=[&]{return Table(r,8,item);};
  for(auto& v:e.HeaderRectangles)v=rectangle();e.HeaderScale=std::bit_cast<float>(r.U32());
  e.CommonRectangle=rectangle();e.CommonItems=items();e.OptionsRectangle=rectangle();e.OptionsRows=Table(r,4,items);
  e.PrefixBytes=bytes.size()-r.Remaining();return e;
}
ButtonHelpStyle ButtonHelpStyle::Decode(std::span<const uint8_t> bytes){
  PayloadReader r(bytes);for(size_t i=0;i<3;++i)r.U8();
  const auto words=[&](uint32_t n){r.CheckCount(n,4);for(uint32_t i=0;i<n;++i)r.U32();};
  const auto records=[&](uint32_t stride){const auto n=r.U32();r.CheckCount(n,size_t(stride)*4);words(n*stride);};
  for(size_t i=0;i<4;++i)r.String();words(2);for(size_t i=0;i<9;++i)r.String();words(30);
  records(6);words(8);records(6);words(4);records(1);
  ButtonHelpStyle s;s.Keys=Table(r,4,[&]{return r.String();});s.Separator=r.String();
  s.TextScale=std::bit_cast<float>(r.U32());s.Gap=r.I32();s.PrefixBytes=bytes.size()-r.Remaining();
  if(!std::isfinite(s.TextScale) || s.TextScale<0)throw FormatError("invalid button-help text scale");return s;
}
std::vector<UiTextBlit> BuildButtonHelp(std::span<const ButtonHelpItem> items,
    std::array<int32_t,4> rectangle,const ButtonHelpStyle& style,const GsfFont& font,
    uint32_t utility,uint32_t nominal,int32_t alignment,float opacity){
  if(alignment<0 || alignment>8 || !std::isfinite(opacity) || opacity<0 || utility>uint32_t(INT32_MAX))throw FormatError("invalid button-help draw inputs");
  const auto scaled=float(utility)*style.TextScale;
  if(!std::isfinite(scaled) || scaled<0 || scaled>=2147483648.0f)throw FormatError("invalid button-help font size");
  const auto size=uint32_t(Integer(scaled));
  const auto measure=[&](std::u16string_view text,uint32_t n){return std::bit_cast<int32_t>(MeasurePvfText(font,text,n,nominal));};
  const auto key=[&](const ButtonHelpItem& item)->const std::u16string&{if(item.Key<0 || size_t(item.Key)>=style.Keys.size())throw FormatError("button-help key outside authored labels");return style.Keys[size_t(item.Key)];};
  int32_t total=items.empty()?Sub(0,style.Gap):std::bit_cast<int32_t>(uint32_t(items.size()-1)*uint32_t(style.Gap));
  for(const auto& item:items){if(item.Key!=-1){total=Add(total,measure(key(item),utility));total=Add(total,measure(style.Separator,size));}total=Add(total,measure(item.Text,size));}
  const auto horizontal=alignment%3,vertical=alignment/3;
  const auto start=horizontal==1?Sub(rectangle[2],total)/2:horizontal==2?rectangle[2]:0;
  const auto y=vertical==1?rectangle[3]/2:vertical==2?Sub(0,rectangle[3]):0;
  const auto difference=Sub(int32_t(utility),int32_t(size));const auto half=difference>>1;
  const auto alpha=uint8_t(std::min(Integer(opacity*255.0f),255));
  std::vector<UiTextBlit> out;int32_t cursor=0;
  for(const auto& item:items){
    int32_t prefix=0;if(item.Key!=-1)prefix=Add(measure(key(item),utility),measure(style.Separator,size));
    const auto next=Add(Add(Add(cursor,prefix),measure(item.Text,size)),style.Gap);
    const auto x=Add(rectangle[0],horizontal==2?Sub(start,next):Add(start,cursor));
    int32_t drawPrefix=0;
    if(item.Key!=-1){
      out.push_back({key(item),{x,Sub(Add(rectangle[1],y),half)},0,{255,255,255,alpha}});
      drawPrefix=measure(key(item),0);
    }
    out.push_back({item.Text,{Add(x,drawPrefix),Sub(Add(Add(rectangle[1],y),half),int32_t(size)>>1)},size,{0,0,0,alpha}});
    cursor=next;
  }
  return out;
}
}
