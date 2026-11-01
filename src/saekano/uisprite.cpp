#include "uisprite.h"

#include "commands.h"
#include "approachmission.h"
#include "chapter.h"

#include <bit>
#include <cmath>
#include <limits>
#include <algorithm>

namespace Impacto::Saekano {
ScenarioDateEnvironment ScenarioDateEnvironment::Decode(std::span<const uint8_t> script) {
  if(script.size()<19)throw FormatError("truncated scenario date environment");
  PayloadReader r(script.subspan(19));ScenarioDateEnvironment e;
  e.Base=UiSpriteDefinition::ReadLegacy(r);
  auto count=r.U32();r.CheckCount(count,34);
  for(uint32_t i=0;i<count;++i)e.Labels.push_back(UiSpriteDefinition::ReadLegacy(r));
  count=r.U32();r.CheckCount(count,4);
  for(uint32_t i=0;i<count;++i) {
    const auto c=r.U32();e.Colors.push_back({uint8_t(c>>16),uint8_t(c>>8),uint8_t(c),uint8_t(c>>24)});
  }
  e.Month=UiSpriteDefinition::ReadLegacy(r);e.Day=UiSpriteDefinition::ReadLegacy(r);
  count=r.U32();r.CheckCount(count,8);
  for(uint32_t i=0;i<count;++i)e.MonthRanges.push_back({r.I32(),r.I32()});
  return e;
}
void UpdateScenarioDatePalette(SemanticState& state,const ScenarioDateEnvironment& e) {
  const auto normalize=[](int32_t month){return int64_t(month)+(month<9?12:0);};
  const auto month=normalize(state.TimeComponents[0]);
  for(size_t i=0;i<e.MonthRanges.size();++i)
    if(normalize(e.MonthRanges[i][0])<=month && month<=normalize(e.MonthRanges[i][1])) {
      state.DatePalette=i;break;
    }
}
std::vector<UiSpriteBlit> BuildScenarioDate(const SemanticState& state,
    const ScenarioDateEnvironment& e,bool movieActive) {
  if(movieActive || !state.DayEnabled)return {};
  if(!state.DatePalette)throw UnsupportedCommand("scenario date palette is unknown");
  const auto palette=*state.DatePalette;
  const auto month=state.TimeComponents[0],day=state.TimeComponents[1];
  if(palette>=e.Labels.size() || palette>=e.Colors.size() || month<1 || month>12 || day<1 || day>31)
    throw FormatError("scenario date outside authored calendar resources");
  auto base=e.Base;base.Color=e.Colors[palette];
  auto monthSprite=e.Month,daySprite=e.Day;
  monthSprite.SourceX+=(month<9?month+3:month-9)*monthSprite.Width;
  daySprite.SourceX+=((day-1)%10)*daySprite.Width;
  daySprite.SourceY+=((day-1)/10)*(daySprite.Height+1);
  return {ResolveUiSprite({ProfileTextureSection::MainUi,-1,4},base),
    ResolveUiSprite({ProfileTextureSection::MainUi,-1,4},e.Labels[palette]),
    ResolveUiSprite({ProfileTextureSection::MainUi,-1,5},monthSprite),
    ResolveUiSprite({ProfileTextureSection::MainUi,-1,6},daySprite)};
}
ChapterTitleVisualEnvironment ChapterTitleVisualEnvironment::Decode(std::span<const uint8_t> script,
    std::span<const uint8_t> menu) {
  ChapterTitleVisualEnvironment e;
  const auto prefix=ChapterEnvironment::Decode(script).PrefixBytes;
  PayloadReader text(script.subspan(prefix-24,20));
  for(auto& p:e.TextPositions)p={text.I32(),text.I32()};
  e.TextScale=std::bit_cast<float>(text.U32());
  if(!std::isfinite(e.TextScale) || e.TextScale<0)throw FormatError("invalid chapter text scale");
  if(menu.size()<3)throw FormatError("truncated system menu header");
  PayloadReader r(menu.subspan(3));
  const auto words=[&](size_t count){for(size_t i=0;i<count;++i)r.U32();};
  const auto sprites=[&](size_t count){for(size_t i=0;i<count;++i)UiSpriteDefinition::ReadLegacy(r);};
  words(3);sprites(2);words(1);sprites(1);words(4);sprites(1);words(4);
  for(size_t i=0;i<4;++i){const auto count=r.U32();r.CheckCount(count,34);sprites(count);}
  words(3);const auto tableCount=r.U32();r.CheckCount(tableCount,4);words(tableCount);words(5);
  sprites(3);words(9);sprites(1);e.Banner=UiSpriteDefinition::ReadLegacy(r);
  words(4);sprites(2);words(3);
  const auto count=r.U32();r.CheckCount(count,4);
  for(uint32_t i=0;i<count;++i){const auto c=r.U32();e.Colors.push_back({uint8_t(c>>16),uint8_t(c>>8),uint8_t(c),uint8_t(c>>24)});}
  e.MenuPrefixBytes=menu.size()-r.Remaining();return e;
}
std::array<UiTextBlit,2> BuildChapterTitleText(const ChapterTitleVisualEnvironment& e,
    std::span<const std::u16string> titles,uint32_t utilitySize) {
  if(titles.size()!=2)throw FormatError("chapter title strings are unavailable");
  const float scaled=float(utilitySize)*e.TextScale;
  if(!std::isfinite(scaled) || scaled<0 || scaled>1024)throw FormatError("chapter font size outside display budget");
  const auto size=uint32_t(scaled);std::array<UiTextBlit,2> draws;
  for(size_t i=0;i<2;++i)draws[i]={titles[i],{e.TextPositions[i][0],
      std::bit_cast<int32_t>(uint32_t(e.TextPositions[i][1])-size/2)},size,{0,0,0,255}};
  return draws;
}
UiSpriteBlit BuildChapterTitleBanner(const ChapterTitleVisualEnvironment& e,int32_t id) {
  if(id<0 || size_t(id)>=e.Colors.size())throw FormatError("chapter color ID outside authored palette");
  auto sprite=e.Banner;sprite.Color=e.Colors[size_t(id)];
  return ResolveUiSprite({ProfileTextureSection::MainUi,-1,50},sprite);
}
namespace {
int32_t Subtract(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
}
int32_t Convert(float value) {
  if (std::isnan(value)) return 0;
  if (value >= 2147483648.0f) return std::numeric_limits<int32_t>::max();
  if (value <= -2147483648.0f) return std::numeric_limits<int32_t>::min();
  return int32_t(value);
}
int32_t Size(int32_t size, float scale) {
  const float product = float(size) * scale;
  return Convert(scale > 0 ? product : -product);
}
int32_t Anchor(int32_t coordinate, int32_t size, int32_t alignment) {
  if (alignment == 1) return Subtract(coordinate, size / 2);
  if (alignment == 2) return Subtract(coordinate, size);
  return coordinate;
}
}  // namespace
std::array<std::array<float, 4>, 4> ResolveUiQuad(
    const UiSpriteBlit& blit, uint32_t textureWidth, uint32_t textureHeight) {
  if (!textureWidth || !textureHeight)
    throw FormatError("zero UI texture dimension");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const float x = float(blit.Destination[0]), y = float(blit.Destination[1]);
  const float right = float(add(blit.Destination[0], blit.Destination[2]));
  const float bottom = float(add(blit.Destination[1], blit.Destination[3]));
  const float u = float(blit.Source[0]) / float(textureWidth),
              v = float(blit.Source[1]) / float(textureHeight);
  const float endU =
      float(uint32_t(blit.Source[0]) + uint32_t(blit.Source[2])) /
      float(textureWidth);
  const float endV =
      float(uint32_t(blit.Source[1]) + uint32_t(blit.Source[3])) /
      float(textureHeight);
  std::array<std::array<float, 4>, 4> result{{{x, y, u, v},
                                            {right, y, endU, v},
                                            {x, bottom, u, endV},
                                            {right, bottom, endU, endV}}};
  if (blit.Rotation) {
    const auto& rotation = *blit.Rotation;
    if (!std::isfinite(rotation.Radians))
      throw FormatError("nonfinite UI sprite rotation");
    const float sine = std::sin(rotation.Radians),
                cosine = std::cos(rotation.Radians);
    const float localX = float(Subtract(blit.Destination[0], rotation.Pivot[0])),
                localY = float(Subtract(blit.Destination[1], rotation.Pivot[1]));
    for (size_t i = 0; i < result.size(); ++i) {
      const float px = localX + (i % 2 ? float(blit.Destination[2]) : 0.0f),
                  py = localY + (i / 2 ? float(blit.Destination[3]) : 0.0f);
      // Native VMLA is separate multiply/add, with the y term first.
      float rotatedX = py * -sine, rotatedY = py * cosine;
      rotatedX += px * cosine;
      rotatedY += px * sine;
      result[i][0] = rotatedX + float(rotation.Pivot[0]);
      result[i][1] = rotatedY + float(rotation.Pivot[1]);
    }
  }
  return result;
}
UiSpriteDefinition UiSpriteDefinition::Read(PayloadReader& reader) {
  UiSpriteDefinition result;
  result.Parameter = reader.I32();
  result.Alignment = reader.I32();
  result.X = reader.I32();
  result.Y = reader.I32();
  result.SourceX = reader.I32();
  result.SourceY = reader.I32();
  result.Width = reader.I32();
  result.Height = reader.I32();
  const auto color = reader.U32();
  result.Color = {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color),
                  uint8_t(color >> 24)};
  return result;
}
UiSpriteDefinition UiSpriteDefinition::ReadLegacy(PayloadReader& reader) {
  UiSpriteDefinition result;
  result.Parameter = std::bit_cast<int16_t>(reader.U16());
  result.Alignment = reader.I32();
  result.X = reader.I32();
  result.Y = reader.I32();
  result.SourceX = reader.I32();
  result.SourceY = reader.I32();
  result.Width = reader.I32();
  result.Height = reader.I32();
  const auto color = reader.U32();
  result.Color = {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color),
                  uint8_t(color >> 24)};
  return result;
}
SharedSelectionEnvironment SharedSelectionEnvironment::Decode(std::span<const uint8_t> bytes) {
  if(bytes.size()<3)throw FormatError("truncated shared selection header");
  SharedSelectionEnvironment result;std::copy_n(bytes.begin(),3,result.Header.begin());
  PayloadReader reader(bytes.subspan(3));result.Font=reader.U16();
  for(auto& word:result.Scalars)word=reader.I32();
  const auto color=[&](){const auto packed=reader.U32();return std::array<uint8_t,4>{
      uint8_t(packed>>16),uint8_t(packed>>8),uint8_t(packed),uint8_t(packed>>24)};};
  for(auto& value:result.Colors)value=color();
  for(auto& sprite:result.Sprites)sprite=UiSpriteDefinition::Read(reader);
  for(auto& table:result.PointTables) {
    const auto count=reader.U32();reader.CheckCount(count,8);table.reserve(count);
    for(uint32_t i=0;i<count;++i)table.push_back({reader.I32(),reader.I32()});
  }
  for(auto& word:result.LayoutWords)word=reader.I32();
  result.TextScale=std::bit_cast<float>(reader.U32());
  if(!std::isfinite(result.TextScale) || result.TextScale<0)
    throw FormatError("invalid shared selection text scale");
  for(auto& value:result.TextColors)value=color();
  result.PrefixBytes=bytes.size()-reader.Remaining();return result;
}
OrdinaryMessageEnvironment OrdinaryMessageEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < 3)
    throw FormatError("truncated ordinary message environment");
  PayloadReader reader(bytes.subspan(3));
  for (size_t i = 0; i < 3; ++i) reader.U32();
  OrdinaryMessageEnvironment result;
  for (auto& sprite : result.Frame)
    sprite = UiSpriteDefinition::ReadLegacy(reader);
  for (auto& value : result.HitRegion) value = reader.I32();
  for (auto& sprite : result.Nameplate)
    sprite = UiSpriteDefinition::ReadLegacy(reader);
  for (auto& sprite : result.Cursors)
    sprite = UiSpriteDefinition::ReadLegacy(reader);
  for (auto& sprite : result.Auxiliary)
    sprite = UiSpriteDefinition::ReadLegacy(reader);
  result.AutoRotationDuration = reader.I32();
  if (result.AutoRotationDuration < 0)
    throw FormatError("negative ordinary auto rotation duration");
  result.MenuHint = UiSpriteDefinition::ReadLegacy(reader);
  for (auto& sprite : result.MenuSprites)
    sprite = UiSpriteDefinition::ReadLegacy(reader);
  for (auto& value : result.MenuHitRegion) value = reader.I32();
  reader.U32();
  reader.U32();
  const auto count = reader.U32();
  reader.CheckCount(count, 4);
  for (uint32_t i = 0; i < count; ++i) reader.U32();
  reader.I32();
  reader.I32();  // First point belongs to another widget.
  reader.I32();
  result.WindowTravel = reader.I32();
  result.TextPosition = {reader.I32(), reader.I32()};
  result.TextWidth = reader.I32();
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
std::vector<MessageWindowDraw> BuildOrdinaryMessageWindow(
    const OrdinaryMessageEnvironment& environment,
    const OrdinaryMessageWindowState& state) {
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto slide = std::bit_cast<int32_t>(uint32_t(environment.WindowTravel) *
                                            (100u - state.Openness)) /
                     100;
  const auto alpha = uint8_t(uint32_t(state.OpacitySetting) * 255 / 11);
  std::vector<MessageWindowDraw> draws;
  const auto rendererMode = [&](uint32_t mode, uint32_t parameter) {
    MessageWindowDraw draw;
    draw.Kind = MessageWindowDrawKind::RendererState;
    draw.Index = mode;
    draw.Parameter = parameter;
    draws.push_back(draw);
  };
  const auto sprite = [&](UiSpriteDefinition definition, uint32_t entry,
                          int32_t cursorOffset = 0) {
    definition.X = add(definition.X, state.Quake[0]);
    definition.Y =
        add(add(add(definition.Y, slide), state.Quake[1]), cursorOffset);
    definition.Color[3] = alpha;
    MessageWindowDraw alphaTest;
    alphaTest.Kind = MessageWindowDrawKind::AlphaTest;
    alphaTest.Index = 1;
    draws.push_back(alphaTest);
    MessageWindowDraw draw;
    draw.Sprite =
        ResolveUiSprite({ProfileTextureSection::MainUi, -1, entry}, definition);
    draws.push_back(draw);
  };
  const auto widgetCall = [&](uint32_t widget, uint32_t opacity) {
    MessageWindowDraw draw;
    draw.Kind = MessageWindowDrawKind::Widget;
    draw.Index = widget;
    draw.Parameter = opacity;
    if (widget) draw.Offset = {state.Quake[0], add(slide, state.Quake[1])};
    draws.push_back(draw);
  };
  rendererMode(0, 0);
  sprite(environment.Frame[0], 0);
  sprite(environment.Frame[1], 1);
  widgetCall(0, 255);  // This widget call deliberately uses opaque white.
  if (state.SpeakerId != -1) {
    sprite(environment.Nameplate[0], 9);
    sprite(environment.Nameplate[1], 9);
    sprite(environment.Nameplate[2], uint32_t(state.SpeakerId) + 41u);
  }
  widgetCall(1, alpha);
  widgetCall(2, alpha);
  if (state.MessageCommand && state.CursorVisible)
    for (auto definition : environment.Cursors)
      sprite(definition, 2, Convert(state.CursorOffset));
  rendererMode(state.RendererMode >= 1 && state.RendererMode <= 3
                   ? state.RendererMode
                   : 0,
               state.RendererMode >= 1 && state.RendererMode <= 3
                   ? state.RendererParameter
                   : 0);
  return draws;
}
std::vector<UiSpriteBlit> BuildOrdinaryMessageWidget(
    const OrdinaryMessageEnvironment& environment, uint32_t widget,
    bool skipActive, bool skipGesture, bool autoActive,
    std::array<int32_t, 2> offset, uint8_t alpha,
    std::optional<int32_t> autoDegrees) {
  if (widget > 2) throw FormatError("unknown ordinary widget");
  if (widget == 0) return {};  // Transparent hit region; no visible content.
  if (widget == 2 && autoActive && !autoDegrees)
    throw UnsupportedCommand("ordinary active-auto rotation sample is unknown");
  const std::array<size_t, 3> indices =
      widget == 1 ? std::array<size_t, 3>{0, skipActive ? 2u : 1u,
                                          skipGesture ? 4u : 3u}
                  : std::array<size_t, 3>{5, 6, autoActive ? 8u : 7u};
  std::vector<UiSpriteBlit> result;
  for (auto index : indices) {
    auto definition = environment.Auxiliary[index];
    // The native factory ignores descriptor tint and uses opaque white. Its
    // auto row1 accidentally borrows row0's width; honor row1's own width.
    definition.Color = {255, 255, 255, alpha};
    definition.X =
        std::bit_cast<int32_t>(uint32_t(definition.X) + uint32_t(offset[0]));
    definition.Y =
        std::bit_cast<int32_t>(uint32_t(definition.Y) + uint32_t(offset[1]));
    auto blit = ResolveUiSprite({ProfileTextureSection::MainUi, -1, 3}, definition);
    if (widget == 2 && autoActive && index == 8) {
      const float product = float(*autoDegrees) * std::bit_cast<float>(0x40490fd8u);
      blit.Rotation = UiSpriteRotation{
          product / 180.0f,
          {std::bit_cast<int32_t>(uint32_t(blit.Destination[0]) +
                                 uint32_t(blit.Destination[2] / 2)),
           std::bit_cast<int32_t>(uint32_t(blit.Destination[1]) +
                                 uint32_t(blit.Destination[3] / 2))}};
    }
    result.push_back(std::move(blit));
  }
  return result;
}
MessageCursorEnvironment MessageCursorEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < 3) throw FormatError("truncated message cursor flags");
  PayloadReader reader(bytes.subspan(3));
  for (size_t i = 0; i < 3; ++i) reader.U32();  // Colors.
  for (size_t i = 0; i < 2; ++i) UiSpriteDefinition::ReadLegacy(reader);
  for (size_t i = 0; i < 4; ++i) reader.U32();  // Rectangle.
  for (size_t i = 0; i < 3; ++i) UiSpriteDefinition::ReadLegacy(reader);
  MessageCursorEnvironment result;
  for (auto& sprite : result.Sprites)
    sprite = UiSpriteDefinition::ReadLegacy(reader);
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
UiSpriteBlit ResolveUiSprite(ProfileTextureKey texture,
                             const UiSpriteDefinition& sprite, float scaleX,
                             float scaleY) {
  const auto width = Size(sprite.Width, scaleX),
             height = Size(sprite.Height, scaleY);
  UiSpriteBlit result;
  result.Texture = texture;
  result.Destination = {Anchor(sprite.X, width, sprite.Alignment % 3),
                        Anchor(sprite.Y, height, sprite.Alignment / 3), width,
                        height};
  result.Source = {sprite.SourceX, sprite.SourceY,
                   scaleX > 0 ? sprite.Width : Subtract(0, sprite.Width),
                   scaleY > 0 ? sprite.Height : Subtract(0, sprite.Height)};
  result.Color = sprite.Color;
  return result;
}
std::vector<UiSpriteBlit> BuildApproachGaugeSprites(
    const ApproachVisualEnvironment& environment,int32_t gauge,bool hidden) {
  if(hidden)return {};
  if(gauge<0 || gauge>100)throw FormatError("approach gauge outside native consumer range");
  const auto& frame=environment.Sprites[3];
  auto fill=environment.Sprites[4];
  // VDIV then VMUL and VCVT truncate the height. Native anchors/crops upward
  // from the authored lower edge, with no interpolation of the gauge width.
  const auto height=Convert(float(fill.Height)*(float(gauge)/100.0f));
  fill.SourceY=Subtract(fill.SourceY,Subtract(height,fill.Height));
  fill.Height=height;
  return {ResolveUiSprite({ProfileTextureSection::MainUi,-1,24},frame),
          ResolveUiSprite({ProfileTextureSection::MainUi,-1,25},fill)};
}
std::vector<UiSpriteBlit> BuildApproachMissionSprites(
    const ApproachVisualEnvironment& environment,int32_t character,
    const ApproachMissionState& mission) {
  if(!mission.Visible)return {};
  if(!mission.Started || character<0 || size_t(character)>=environment.CharacterLabels.size())
    throw UnsupportedCommand("approach mission drawing inputs are unavailable");
  const auto offset=mission.Tweens[4].Current;
  std::vector<UiSpriteBlit> result;
  const auto emit=[&](UiSpriteDefinition sprite,uint32_t entry,int32_t alpha) {
    sprite.Y=Subtract(sprite.Y,Subtract(0,offset));sprite.Color[3]=uint8_t(alpha);
    result.push_back(ResolveUiSprite({ProfileTextureSection::MainUi,-1,entry},sprite));
  };
  emit(environment.Sprites[0],22,mission.Tweens[0].Current);
  auto left=environment.Sprites[1];left.X=mission.Tweens[1].Current;
  emit(left,32,mission.Tweens[5].Current);
  auto right=environment.Sprites[2];right.X=mission.Tweens[2].Current;
  emit(right,32,mission.Tweens[5].Current);
  emit(environment.CharacterLabels[size_t(character)],uint32_t(character)+26,mission.Tweens[3].Current);
  return result;
}
ProfileBackdropEnvironment ProfileBackdropEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < 3 || bytes.size() > 32 * 1024 * 1024)
    throw FormatError("invalid profile backdrop environment extent");
  PayloadReader reader(bytes.subspan(3));
  for (size_t i = 0; i < 3; ++i) reader.U32();
  ProfileBackdropEnvironment result;
  result.IntroFade = reader.I32();
  for (auto& sprite : result.Sprites) sprite = UiSpriteDefinition::Read(reader);
  result.WavePeriod = reader.I32();
  result.PrefixBytes = bytes.size() - reader.Remaining();
  if (result.IntroFade < 0 || result.WavePeriod < 0)
    throw FormatError("negative profile backdrop transition duration");
  return result;
}
std::vector<UiSpriteBlit> BuildProfileMenuChrome(
    const ProfileMenuChromeEnvironment& environment, int32_t phase,
    int32_t resultSlide) {
  const auto key = [](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::Interface, -1, entry};
  };
  std::vector<UiSpriteBlit> result;
  if (phase >= 12) {
    if (environment.MarkerPositions.size() < 3)
      throw FormatError(
          "profile result markers require three authored positions");
    for (size_t i = 0; i < 3; ++i) {
      auto sprite = environment.Marker;
      const auto& position = environment.MarkerPositions[i];
      sprite.X = std::bit_cast<int32_t>(
          uint32_t(position[0]) +
          uint32_t(resultSlide) * uint32_t(environment.MarkerStep[0]));
      sprite.Y =
          Subtract(position[1],
                   std::bit_cast<int32_t>(uint32_t(resultSlide) *
                                          uint32_t(environment.MarkerStep[1])));
      result.push_back(ResolveUiSprite(key(17), sprite));
    }
  }
  // Native raw backend receives independent destination size; its source
  // extent remains the authored sprite extent, rather than scaling the source.
  auto stretch = ResolveUiSprite(key(24), environment.Stretch);
  stretch.Destination = {
      Anchor(environment.Stretch.X, environment.StretchSize[0],
             environment.Stretch.Alignment % 3),
      Anchor(environment.Stretch.Y, environment.StretchSize[1],
             environment.Stretch.Alignment / 3),
      environment.StretchSize[0], environment.StretchSize[1]};
  result.push_back(stretch);
  result.push_back(ResolveUiSprite(key(24), environment.Header));
  result.push_back(ResolveUiSprite(key(24), phase <= 5
                                                ? environment.EarlyOverlay
                                                : environment.LateOverlay));
  result.push_back(ResolveUiSprite(key(3), environment.Frame));
  result.push_back(ResolveUiSprite(key(11), environment.Footer));
  return result;
}
std::vector<UiRenderCommand> BuildProfileItemFramePass(
    const ProfileItemFrameEnvironment& environment, int32_t phase,
    int32_t canvasWidth, int32_t canvasHeight,
    std::span<const std::array<int32_t, 2>> frameOffsets) {
  if (canvasWidth <= 0 || canvasHeight <= 0)
    throw FormatError("invalid profile mask canvas extent");
  if (environment.Positions.size() < 8)
    throw FormatError("profile item frames require eight authored positions");
  if (!frameOffsets.empty() && frameOffsets.size() != 8)
    throw FormatError("profile carousel frame offsets require eight rows");
  std::vector<UiRenderCommand> result;
  const auto state = [&](UiRenderKind kind, int32_t mode = 0) {
    UiRenderCommand command;
    command.Kind = kind;
    command.Mode = mode;
    result.push_back(command);
  };
  const auto rectangle = [&](std::array<int32_t, 4> bounds,
                             std::array<uint8_t, 4> color) {
    UiRenderCommand command;
    command.Kind = UiRenderKind::Rectangle;
    command.Rectangle = bounds;
    command.Color = color;
    result.push_back(command);
  };
  state(UiRenderKind::ResetDepthStencilBackground);
  state(UiRenderKind::Stencil, 1);  // Replace with reference1.
  state(UiRenderKind::DepthTest,
        0);  // Never; stencil depth-fail still replaces.
  state(UiRenderKind::AlphaTest, 6);  // Greater than zero.
  rectangle({0, 0, canvasWidth, canvasHeight}, {0, 0, 0, 1});
  state(UiRenderKind::Stencil, 2);  // Replace with reference0.
  rectangle({500, 96, 460, 300}, {255, 255, 255, 255});
  state(UiRenderKind::Stencil, 3);    // Equal to zero, keep.
  state(UiRenderKind::AlphaTest, 3);  // Not equal to zero.
  state(UiRenderKind::DepthTest, 5);  // Less or equal, depth writes enabled.
  for (size_t i = 0; i < 8; ++i) {
    auto sprite = environment.Sprite;
    sprite.X = environment.Positions[i][0];
    sprite.Y = environment.Positions[i][1];
    if (!frameOffsets.empty()) {
      sprite.X = std::bit_cast<int32_t>(uint32_t(sprite.X) +
                                        uint32_t(frameOffsets[i][0]));
      sprite.Y = std::bit_cast<int32_t>(uint32_t(sprite.Y) +
                                        uint32_t(frameOffsets[i][1]));
    }
    // Factory uses white local color, not the descriptor's tint. Widget draw
    // multiplies by the parent's phase-dependent byte color.
    const uint8_t tint = phase <= 5 ? 200 : 255;
    sprite.Color = {tint, tint, tint, 255};
    UiRenderCommand command;
    command.Kind = UiRenderKind::Sprite;
    command.Sprite =
        ResolveUiSprite({ProfileTextureSection::Interface, -1, 18}, sprite);
    result.push_back(command);
  }
  return result;
}
std::vector<UiSpriteBlit> BuildProfileBackdrop(
    const ProfileBackdropEnvironment& environment, int32_t waveOffset) {
  const auto key = [](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::Interface, -1, entry};
  };
  std::vector<UiSpriteBlit> result;
  result.push_back(ResolveUiSprite(key(2), environment.Sprites[0]));
  const int32_t remaining = Subtract(environment.Sprites[1].Width, waveOffset);
  for (size_t i = 1; i < 3; ++i) {
    auto first = environment.Sprites[i];
    first.SourceX = waveOffset;
    first.Width = remaining;
    result.push_back(ResolveUiSprite(key(uint32_t(21 + i)), first));
    auto second = environment.Sprites[i];
    second.X = remaining;  // Native deliberately retains full source width.
    result.push_back(ResolveUiSprite(key(uint32_t(21 + i)), second));
  }
  return result;
}
std::vector<UiRenderCommand> BuildProfileResultLayers(
    const ModeResultVisualEnvironment& environment,
    const UiSpriteDefinition& reflection, const ProfileResultState& result,
    int32_t canvasWidth, int32_t canvasHeight) {
  std::vector<UiRenderCommand> commands;
  if (!result.Visible) return commands;
  if (canvasWidth <= 0 || canvasHeight <= 0)
    throw FormatError("invalid profile result mask canvas extent");
  if (result.CharacterId < 0 ||
      size_t(result.CharacterId) >= environment.CharacterColors.size() ||
      result.Event.Values[1] < 0)
    throw FormatError("profile result character color/event image unavailable");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto percentage = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) * uint32_t(b)) / 100;
  };
  const auto state = [&](UiRenderKind kind, int32_t mode,std::optional<bool> enabled=std::nullopt) {
    UiRenderCommand command;
    command.Kind = kind;
    command.Mode = mode;
    command.TestEnabled=enabled;
    commands.push_back(command);
  };
  const auto rectangle = [&](std::array<int32_t, 4> bounds,
                             std::array<uint8_t, 4> color) {
    UiRenderCommand command;
    command.Kind = UiRenderKind::Rectangle;
    command.Rectangle = bounds;
    command.Color = color;
    commands.push_back(command);
  };
  const auto sprite = [&](ProfileTextureKey key, UiSpriteDefinition definition,
                          float scaleY = 1) {
    UiRenderCommand command;
    command.Kind = UiRenderKind::Sprite;
    command.Sprite = ResolveUiSprite(key, definition, 1, scaleY);
    commands.push_back(command);
  };
  const auto main = [](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::MainUi, -1, entry};
  };
  const auto fade = uint8_t(percentage(result.Primary.Current, 255));
  rectangle(
      {0, 0, 960, 544},
      {0, 0, 0,
       uint8_t(percentage(environment.OverlayAlpha, result.Primary.Current))});
  auto definition = reflection;
  definition.Color[3] = fade;
  sprite(main(22), definition, -1);
  definition = environment.Banner;
  definition.Color[3] = fade;
  sprite(main(22), definition);
  auto mask = environment.Mask;
  mask.X = add(mask.X, result.HorizontalOffset.Current);
  mask.Color = environment.CharacterColors[size_t(result.CharacterId)];
  mask.Color[3] = uint8_t(result.HorizontalAlpha.Current);
  sprite(main(10), mask);
  state(UiRenderKind::Stencil, 1);
  state(UiRenderKind::DepthTest, 0,true);
  state(UiRenderKind::AlphaTest, 6,true);
  rectangle({0, 0, canvasWidth, canvasHeight}, {0, 0, 0, 1});
  state(UiRenderKind::Stencil, 2);
  mask.Color = {255, 255, 255, 255};
  sprite(main(10), mask);
  state(UiRenderKind::Stencil, 3);
  state(UiRenderKind::AlphaTest, 3,true);
  state(UiRenderKind::DepthTest, 5,true);
  definition = environment.MaskedContent;
  definition.X = add(definition.X, result.HorizontalOffset.Current);
  definition.Color[3] = uint8_t(result.HorizontalAlpha.Current);
  sprite(main(13), definition);
  state(UiRenderKind::AlphaTest, 3,false);
  state(UiRenderKind::DepthTest, 5,false);
  state(UiRenderKind::Stencil, 0);
  definition = environment.Frame;
  definition.X = add(definition.X, result.HorizontalOffset.Current);
  definition.Color[3] = uint8_t(result.HorizontalAlpha.Current);
  sprite(main(11), definition);
  definition = environment.EventIcon;
  definition.X = add(definition.X, result.VerticalOffset.Current);
  definition.Color[3] = uint8_t(result.VerticalAlpha.Current);
  sprite({ProfileTextureSection::Character, result.CharacterId,
          uint32_t(result.Event.Values[1])},
         definition);
  definition = environment.ProgressFill;
  definition.Width =
      percentage(definition.Width, result.EntryCompletion.Current);
  definition.Color[3] = uint8_t(result.HorizontalAlpha.Current);
  sprite(main(12), definition);
  return commands;
}
std::vector<UiRenderCommand> BuildApproachResultLayers(
    const ModeResultVisualEnvironment& e,const ApproachResultState& s,
    int32_t character,int32_t canvasWidth,int32_t canvasHeight) {
  std::vector<UiRenderCommand> commands;
  if(!s.Visible)return commands;
  if(canvasWidth<=0 || canvasHeight<=0 || character<0 ||
      size_t(character)>=e.CharacterColors.size() || s.Outcome>1)
    throw FormatError("approach result draw inputs outside authored banks");
  const auto add=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
  const auto percent=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)*uint32_t(b))/100;};
  const auto state=[&](UiRenderKind kind,int32_t mode,std::optional<bool> enabled=std::nullopt){
    UiRenderCommand c;c.Kind=kind;c.Mode=mode;c.TestEnabled=enabled;commands.push_back(c);
  };
  const auto rectangle=[&](std::array<int32_t,4> bounds,std::array<uint8_t,4> color){
    UiRenderCommand c;c.Kind=UiRenderKind::Rectangle;c.Rectangle=bounds;c.Color=color;commands.push_back(c);
  };
  const auto sprite=[&](ProfileTextureKey key,const UiSpriteDefinition& definition){
    UiRenderCommand c;c.Kind=UiRenderKind::Sprite;c.Sprite=ResolveUiSprite(key,definition);commands.push_back(c);
  };
  const auto main=[](uint32_t entry){return ProfileTextureKey{ProfileTextureSection::MainUi,-1,entry};};
  rectangle({0,0,960,544},{0,0,0,uint8_t(percent(e.OverlayAlpha,s.Tweens[0].Current))});
  auto definition=e.Banner;definition.Color[3]=uint8_t(percent(s.Tweens[0].Current,255));
  sprite(main(22),definition); // The approach draw has no profile reflection.
  auto mask=e.Mask;mask.X=add(mask.X,s.Tweens[1].Current);
  mask.Color=e.CharacterColors[size_t(character)];mask.Color[3]=uint8_t(s.Tweens[2].Current);
  sprite(main(10),mask);
  state(UiRenderKind::Stencil,1);state(UiRenderKind::DepthTest,0,true);state(UiRenderKind::AlphaTest,6,true);
  rectangle({0,0,canvasWidth,canvasHeight},{0,0,0,1});state(UiRenderKind::Stencil,2);
  mask.Color={255,255,255,255};sprite(main(10),mask);
  state(UiRenderKind::Stencil,3);state(UiRenderKind::AlphaTest,3,true);state(UiRenderKind::DepthTest,5,true);
  definition=e.MaskedContent;definition.X=add(definition.X,s.Tweens[1].Current);definition.Color[3]=uint8_t(s.Tweens[2].Current);
  sprite(main(13),definition);
  state(UiRenderKind::AlphaTest,3,false);state(UiRenderKind::DepthTest,5,false);state(UiRenderKind::Stencil,0);
  definition=e.Frame;definition.X=add(definition.X,s.Tweens[1].Current);definition.Color[3]=uint8_t(s.Tweens[2].Current);
  sprite(main(11),definition);
  definition=e.EventIcon;definition.X=add(definition.X,s.Tweens[3].Current);definition.Color[3]=uint8_t(s.Tweens[4].Current);
  sprite({ProfileTextureSection::Character,character,s.Outcome==0?2u:0u},definition);
  definition=e.ProgressFill;definition.Width=percent(definition.Width,s.Tweens[5].Current);definition.Color[3]=uint8_t(s.Tweens[2].Current);
  sprite(main(12),definition);
  return commands;
}
std::vector<UiSpriteBlit> BuildApproachResultOverlays(
    const ModeResultVisualEnvironment& e,const MessageCursorEnvironment& cursors,
    const ApproachResultState& s,float cursorOffset) {
  std::vector<UiSpriteBlit> draws;if(!s.Visible)return draws;
  if(s.Outcome>1)throw FormatError("approach result outcome outside authored table");
  const auto main=[](uint32_t entry){return ProfileTextureKey{ProfileTextureSection::MainUi,-1,entry};};
  if(s.Cursor)for(size_t i=0;i<2;++i) {
    auto definition=cursors.Sprites[i];definition.X=e.CursorPositions[i][0];
    definition.Y=std::bit_cast<int32_t>(uint32_t(e.CursorPositions[i][1])+uint32_t(Convert(cursorOffset)));
    draws.push_back(ResolveUiSprite(main(2),definition));
  }
  const size_t category=s.Outcome==0?2:0;
  auto definition=e.Outcomes[category];definition.Color[3]=uint8_t(s.Tweens[6].Current);
  draws.push_back(ResolveUiSprite(main(uint32_t(14+category)),definition));
  return draws;
}
ProfileResponseUiDraws BuildProfileResponseUi(
    const ProfileResponseVisualEnvironment& environment, bool textRevealed,
    float cursorOffset) {
  ProfileResponseUiDraws result;
  result.Window = ResolveUiSprite({ProfileTextureSection::Interface, -1, 1},
                                  environment.Window);
  if (textRevealed) {
    for (auto sprite : environment.Cursors) {
      sprite.Y = std::bit_cast<int32_t>(uint32_t(sprite.Y) +
                                        uint32_t(Convert(cursorOffset)));
      result.Cursors.push_back(
          ResolveUiSprite({ProfileTextureSection::Interface, -1, 0}, sprite));
    }
  }
  return result;
}
std::vector<UiSpriteBlit> BuildProfileResultOverlays(
    const ModeResultVisualEnvironment& environment,
    const MessageCursorEnvironment& cursors, const ProfileResultState& result,
    bool textRevealed, float cursorOffset) {
  if (!result.Visible) return {};
  const auto category = result.Event.Values[1];
  if (category < 0 || category >= 3)
    throw FormatError("profile outcome outside authored category table");
  const auto key = [](uint32_t entry) {
    return ProfileTextureKey{ProfileTextureSection::MainUi, -1, entry};
  };
  std::vector<UiSpriteBlit> draws;
  // Match this draw gate's unsigned deadline comparison, including wrap.
  if (textRevealed && result.TextAlpha.Current == result.TextAlpha.To &&
      result.TextAlpha.Epoch + result.TextAlpha.Duration <=
          result.TextAlpha.Sample) {
    for (size_t i = 0; i < 2; ++i) {
      auto sprite = cursors.Sprites[i];
      sprite.X = environment.CursorPositions[i][0];
      sprite.Y =
          std::bit_cast<int32_t>(uint32_t(environment.CursorPositions[i][1]) +
                                 uint32_t(Convert(cursorOffset)));
      draws.push_back(ResolveUiSprite(key(2), sprite));
    }
  }
  auto outcome = environment.Outcomes[size_t(category)];
  outcome.Color[3] = uint8_t(result.TextAlpha.Current);
  draws.push_back(ResolveUiSprite(key(uint32_t(14 + category)), outcome));
  return draws;
}
}  // namespace Impacto::Saekano
