#include "slotcards.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
namespace Impacto::Saekano {
namespace {
int32_t Wrap(uint32_t v){return std::bit_cast<int32_t>(v);}
int32_t Add(int32_t a,int32_t b){return Wrap(uint32_t(a)+uint32_t(b));}
int32_t Truncate(float v){if(std::isnan(v))return 0;if(v>=2147483648.f)return INT32_MAX;if(v<=-2147483648.f)return INT32_MIN;return int32_t(v);}
std::u16string Wide(std::span<const uint16_t> v){return {v.begin(),v.end()};}
std::u16string CString(std::u16string v){v.resize(v.find(u'\0')==v.npos?v.size():v.find(u'\0'));return v;}
std::u16string Format(std::u16string_view format,std::span<const uint32_t> args){
  std::u16string out;size_t arg=0;
  for(size_t i=0;i<format.size();++i){
    if(format[i]!=u'%'){out.push_back(format[i]);continue;}
    if(++i>=format.size())throw FormatError("truncated slot text format");
    if(format[i]==u'%'){out.push_back(u'%');continue;}
    const bool zero=format[i]==u'0';if(zero)++i;uint32_t width=0;
    while(i<format.size() && format[i]>=u'0' && format[i]<=u'9'){width=width*10+uint32_t(format[i++]-u'0');if(width>128)throw FormatError("slot format width exceeds native buffer");}
    if(i>=format.size() || format[i]!=u'd' || arg>=args.size())throw UnsupportedCommand("unsupported slot numeric format");
    char buffer[32];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),Wrap(args[arg++]));std::u16string digits(buffer,result.ptr);
    if(digits.size()<width){const auto count=size_t(width)-digits.size();if(zero && digits[0]==u'-')digits.insert(1,count,u'0');else digits.insert(0,count,zero?u'0':u' ');}
    out+=digits;if(out.size()>=128)throw FormatError("slot text exceeds native formatting buffer");
  }
  if(arg!=args.size())throw FormatError("slot format argument count differs");
  // Original19a6c0 table: ASCII digits and space only.
  for(auto& c:out){if(c>=u'0' && c<=u'9')c=char16_t(c-u'0'+0xff10);else if(c==u' ')c=0x3000;}
  return out;
}
}
std::vector<SlotCardDrawCommand> BuildSlotCards(const SlotEnvironment& e,std::span<const NativeSlotRecord> records,
    int32_t mode,const SlotCardRange& range,uint8_t alpha,const GsfFont& font,uint32_t utility,uint32_t nominal){
  if(records.size()!=60)throw FormatError("slot cards require all60 records");
  std::vector<SlotCardDrawCommand> out;
  const auto size=[&](uint16_t field){const float product=float(utility)*std::bit_cast<float>(e.Scalars.at(field));if(!std::isfinite(product) || product<0 || product>1024)throw FormatError("invalid slot card font scale");return uint32_t(product);};
  const auto text=[&](std::u16string value,SlotEnvironment::Point position,uint32_t pixels,std::array<uint8_t,4> color){SlotCardDrawCommand c;c.Kind=SlotCardDrawKind::Text;c.Text={std::move(value),position,pixels,color};out.push_back(std::move(c));};
  for(int32_t row=std::max(range.Begin,0);row<std::min(range.End,60);++row){
    const auto& record=records[size_t(row)];const bool occupied=record.Bytes2b6[2]!=0,protectedSlot=record.Bytes2b6[1]!=0,newSlot=record.Bytes2b6[0]!=0;
    const auto pitch=Add(e.Sprites.at(0x168).Height,Wrap(e.Scalars.at(0x164)));
    const auto y=Add(Add(Wrap(e.Scalars.at(0x160)),Wrap(uint32_t(pitch)*(uint32_t(row)-uint32_t(range.FirstVisible)))),range.Offsets[0]);
    const auto x=e.Sprites.at(0x168).X;
    const bool focused=row==range.Selected && range.Offsets[0]==0;
    auto palette=SlotEnvironment::Color{255,255,255,255};
    if(mode==0 || mode==1){const auto& colors=e.ColorTables.at(mode==0?0x420:0x430);if(colors.size()<2)throw FormatError("slot palette has fewer than two colors");palette=colors[size_t(row%2)];}
    const auto sprite=[&](ProfileTextureKey key,UiSpriteDefinition s,std::optional<SlotEnvironment::Color> color=std::nullopt,bool aligned=false){
      SlotCardDrawCommand c;if(color)s.Color=*color;const auto w=Truncate(float(s.Width)),h=Truncate(float(s.Height));s.Y=Add(s.Y,y);
      if(aligned){const auto anchor=[](int32_t v,int32_t n,int32_t a){return a==1?Add(v,-n/2):a==2?Add(v,Wrap(0u-uint32_t(n))):v;};s.X=anchor(s.X,w,s.Alignment%3);s.Y=anchor(s.Y,h,s.Alignment/3);}
      c.Sprite={key,{s.X,s.Y,w,h},{s.SourceX,s.SourceY,s.Width,s.Height},s.Color};out.push_back(c);
    };
    const auto system=[&](uint32_t entry,uint16_t shape,std::optional<SlotEnvironment::Color> color=std::nullopt,bool aligned=false){auto s=e.Sprites.at(shape);if(shape!=0x168 && shape!=0x18c)s.X=Add(s.X,x);sprite({ProfileTextureSection::SystemUi,-1,entry},s,color,aligned);};
    palette[3]=focused?alpha:128;system(64,0x168,palette);palette[3]=128;system(71,0x18c,palette);system(65,0x1d4);
    if(!occupied){
      system(72,0x1b0);palette[3]=row==range.Selected?255:128;
      if(mode==0 || mode==1)system(68,mode==0?0x1f8:0x21c,palette);
    }else{
      if(record.ThumbnailPresent && record.Thumbnail){auto s=e.Sprites.at(0x1b0);s.X=Add(s.X,x);sprite({ProfileTextureSection::SlotThumbnail,-1,uint32_t(row)},s);}
      const auto positioned=[&](std::u16string value,uint16_t point,uint16_t scale){const auto p=e.Points.at(point);text(std::move(value),{Add(x,p[0]),Add(y,p[1])},size(scale),{0,0,0,255});};
      positioned(Format(e.Strings.at(0x468),std::span(record.Words2b8).first(3)),0x294,0x2a4);
      positioned(Format(e.Strings.at(0x4a0),std::span(record.Words2b8).subspan(3,2)),0x29c,0x2a4);
      positioned(record.Words1b94[0] || record.Words1b94[1]?Format(e.Strings.at(0x4d8),std::span(record.Words1b94).first(2)):e.Strings.at(0x510),0x2a8,0x2b0);
      auto title=Wide(record.Strings[0]);const auto maximum=Wrap(e.Scalars.at(0x2c0));if(maximum<=0)throw FormatError("invalid slot title limit");
      if(title.size()>size_t(maximum))title=title.substr(0,size_t(maximum)-1)+e.Strings.at(0x580);else title=CString(std::move(title));
      positioned(std::move(title),0x2b4,0x2bc);positioned(Wide(record.Strings[2]),0x2c4,0x2cc);
    }
    system(70,0x240,e.Colors.at(focused?0x418:0x41c));
    const std::array<uint32_t,1> ordinal{uint32_t(row)+1};auto number=Format(u"%02d",ordinal);const auto pixels=size(0x290);const auto p=e.Points.at(0x288);
    text(number,{Add(Add(x,p[0]),-int32_t(MeasurePvfText(font,number,pixels,nominal)>>1)),Add(Add(y,p[1]),-int32_t(pixels>>1))},pixels,focused?SlotEnvironment::Color{0,0,0,255}:SlotEnvironment::Color{255,255,255,255});
    if(newSlot){SlotCardDrawCommand c;c.Kind=SlotCardDrawKind::NewIcon;const auto s=e.Sprites.at(0x264);c.Position={Add(x,s.X),Add(y,s.Y)};out.push_back(c);}
    const SlotEnvironment::Color lockColor{255,255,255,uint8_t(protectedSlot?255:128)};
    system(66,0x2d4,lockColor,true);system(67,0x2f8,lockColor,true);
  }
  return out;
}
}
