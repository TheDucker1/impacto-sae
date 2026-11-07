#include "messagedraw.h"
#include "commands.h"

#include <bit>
#include <cmath>
#include <algorithm>

namespace Impacto::Saekano {
OrdinaryTextEnvironment OrdinaryTextEnvironment::Decode(
    std::span<const uint8_t> message,bool keywords) {
  OrdinaryTextEnvironment result;
  result.Window = OrdinaryMessageEnvironment::Decode(message);
  PayloadReader reader(message.subspan(result.Window.PrefixBytes));
  reader.U16();
  result.NamePosition = {reader.I32(), reader.I32()};
  for (size_t i = 0; i < 9; ++i)
    reader.U32();  // Other point, rect, scales, leading.
  result.RubyYOffset=reader.I32();result.RubyScale=std::bit_cast<float>(reader.U32());
  const auto rubyColor=reader.U32();result.RubyShadowColor={uint8_t(rubyColor>>16),uint8_t(rubyColor>>8),uint8_t(rubyColor),uint8_t(rubyColor>>24)};
  for (size_t i = 0; i < 2; ++i) reader.U32();  // Other message effect fields.
  result.DefaultScale = reader.U32();
  const auto count = reader.U32();
  reader.CheckCount(count, 4);
  if (!count || count > 4096)
    throw FormatError("invalid message font scale count");
  for (uint32_t i = 0; i < count; ++i)
    result.Scales.push_back(std::bit_cast<float>(reader.U32()));
  if (result.DefaultScale >= result.Scales.size())
    throw FormatError("message default font scale is out of range");
  result.BodyScale = result.Scales[result.DefaultScale];
  result.NameScale = std::bit_cast<float>(reader.U32());
  const auto color = [&]() {
    const auto packed = reader.U32();
    return std::array<uint8_t, 4>{uint8_t(packed >> 16), uint8_t(packed >> 8),
                                  uint8_t(packed), uint8_t(packed >> 24)};
  };
  result.NameColor = color();
  color();  // Separate auxiliary color.
  result.UnreadColor = color();
  result.ReadColor = color();
  if (!std::isfinite(result.BodyScale) || result.BodyScale <= 0 ||
      !std::isfinite(result.NameScale) || result.NameScale <= 0)
    throw FormatError("invalid ordinary text font scale");
  result.PrefixBytes = message.size() - reader.Remaining();
  if(keywords) {
    result.KeywordColors=std::array<std::array<uint8_t,4>,2>{color(),color()};
    result.PrefixBytes=message.size()-reader.Remaining();
  }
  return result;
}
std::vector<UiTextBlit> BuildOrdinaryMessageTextDraws(
    const OrdinaryTextEnvironment& environment, const OrdinaryMessageText& text,
    const VoiceTextReveal& reveal, const PlainTextLayout& body,
    const TextFormatEnvironment& format, const GsfFont& font,
    uint32_t utilitySize, uint32_t nominalSize, uint32_t openness,
    std::array<int32_t, 2> quake, std::array<uint8_t, 4> primaryColor,
    bool shadowEnabled) {
  if (body.Scale != environment.BodyScale || reveal.Text != text.Body)
    throw FormatError("ordinary text draw inputs belong to different layouts");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto slide =
      std::bit_cast<int32_t>(uint32_t(environment.Window.WindowTravel) *
                             (100u - openness)) /
      100;
  const std::array<int32_t, 2> bodyOrigin{
      add(environment.Window.TextPosition[0], quake[0]),
      add(add(environment.Window.TextPosition[1], slide), quake[1])};
  std::vector<UiTextBlit> draws;
  if (!text.InheritedName && text.NameEnabled && !text.Name.empty()) {
    const auto pixels = float(utilitySize) * environment.NameScale;
    if (!std::isfinite(pixels) || pixels < 0 || pixels >= 2147483648.0f)
      throw FormatError("ordinary name pixel size is out of range");
    const auto size = uint32_t(pixels);
    const auto width = MeasurePvfText(font, text.Name, size, nominalSize);
    auto color = environment.NameColor;
    color[3] = 255;
    draws.push_back(
        {text.Name,
         {add(add(environment.NamePosition[0], quake[0]), -int32_t(width / 2)),
          add(add(add(environment.NamePosition[1], slide), quake[1]),
              -int32_t(size / 2))},
         size,
         color});
  }
  auto glyphs = BuildRevealingTextDraws(body, reveal, format, bodyOrigin,
                                        utilitySize, primaryColor, 255, shadowEnabled);
  if(!text.Keywords.empty() && !environment.KeywordColors)
    throw UnsupportedCommand("ordinary keyword drawing colors are not bound");
  for(const auto& range:text.Keywords)
    if(range.Begin>range.End || range.End>body.Glyphs.size())throw FormatError("ordinary keyword exceeds body glyphs");
  for(size_t i=0;i<body.Glyphs.size();++i) {
    auto shadow=glyphs[i*2],foreground=glyphs[i*2+1];bool keyword=false;
    for(const auto& range:text.Keywords)if(range.Begin<=i && i<range.End){keyword=true;break;}
    if(keyword)std::copy_n((*environment.KeywordColors)[0].begin(),3,foreground.Color.begin());
    draws.push_back(shadow);draws.push_back(foreground);
    if(keyword) {
      shadow.Text=foreground.Text=format.Following[3];
      draws.push_back(std::move(shadow));draws.push_back(std::move(foreground));
    }
  }
  if(text.Ruby.empty())return draws;
  const auto pixels=float(utilitySize)*environment.RubyScale;
  if(!std::isfinite(pixels) || pixels<0 || pixels>=2147483648.0f)
    throw FormatError("invalid ruby text scale");
  const auto rubySize=uint32_t(pixels);
  for(const auto& range:text.Ruby){
    if(range.Begin>=range.End || range.End>text.Body.size())throw FormatError("ruby range outside ordinary body");
    const PlainTextLayout::Glyph* first=nullptr;uint8_t alpha=0;int32_t extent=0;
    for(size_t i=0;i<body.Glyphs.size();++i){const auto& glyph=body.Glyphs[i];
      if(range.Begin<=glyph.Index && glyph.Index<range.End){
        if(!first){first=&glyph;alpha=uint8_t(reveal.Glyphs.at(i).Alpha.Current);}
        extent=add(extent,int32_t(float(glyph.Advance)*body.Scale));
      }
    }
    if(!first)throw FormatError("ruby base has no laid out glyph");
    const auto measured=MeasurePvfText(font,range.Reading,rubySize,nominalSize);
    const auto x=add(add(add(bodyOrigin[0],first->Position[0]),extent/2),-int32_t(measured/2));
    const auto y=add(add(bodyOrigin[1],first->Position[1]),environment.RubyYOffset);
    if(alpha && shadowEnabled){auto color=environment.RubyShadowColor;color[3]=alpha;
      draws.push_back({range.Reading,{add(x,1),add(y,1)},rubySize,color});}
    auto color=primaryColor;color[3]=alpha;draws.push_back({range.Reading,{x,y},rubySize,color});
  }
  return draws;
}
}  // namespace Impacto::Saekano
