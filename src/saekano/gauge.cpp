#include "gauge.h"

#include "commands.h"
#include "uisprite.h"
#include "approachmission.h"
#include "approachselection.h"
#include "battledialogue.h"
#include "battleselection.h"
#include "battleresult.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
}
int32_t Negate(int32_t a) { return std::bit_cast<int32_t>(0u - uint32_t(a)); }
int32_t Multiply(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) * uint32_t(b));
}
int32_t Truncate(float value) {
  if (!std::isfinite(value) ||
      double(value) < std::numeric_limits<int32_t>::min() ||
      double(value) > std::numeric_limits<int32_t>::max())
    throw FormatError("battle transform exceeds integer range");
  return static_cast<int32_t>(value);  // vcvt.s32.f32, not roundf.
}
const Live2dRecord* ActorRecord(const SemanticState& state, size_t side) {
  const auto found = std::find_if(
      state.Live2d.Records.begin(), state.Live2d.Records.end(),
      [&](const auto& record) {
        return record.LogicalId == state.Battle.SideCharacters[side];
      });
  return found == state.Live2d.Records.end() ? nullptr : &*found;
}
// These are serialized UI records, not their padded native memory sizes.
// Native reader801c4074: short ID + seven words + packed color =34 bytes.
// reader801c3e7c: rectangle16; reader801c43f8: point8.
class EnvironmentReader {
 public:
  explicit EnvironmentReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  void Skip(size_t bytes) {
    if (bytes > bytes_.size() - cursor_)
      throw FormatError("truncated battle environment");
    cursor_ += bytes;
  }
  int32_t Word() {
    PayloadReader reader(bytes_.subspan(cursor_));
    auto value = reader.I32();
    Skip(4);
    return value;
  }
  UiSpriteDescriptor Sprite() {
    PayloadReader reader(bytes_.subspan(cursor_));
    UiSpriteDescriptor sprite;
    sprite.Resource = std::bit_cast<int16_t>(reader.U16());
    for (auto& word : sprite.Words) word = reader.I32();
    Skip(34);
    return sprite;
  }
  UiSpriteDefinition VisualSprite() {
    const auto raw = Sprite();
    const auto color = uint32_t(raw.Words[7]);
    return {raw.Resource, raw.Words[0], raw.Words[1], raw.Words[2],
            raw.Words[3], raw.Words[4], raw.Words[5], raw.Words[6],
            {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color),
             uint8_t(color >> 24)}};
  }
  void Table(size_t width) {
    const auto count = Count(width);
    Skip(count * width);
  }
  size_t Count(size_t width) {
    const auto count = Word();
    if (count < 0 || size_t(count) > (bytes_.size() - cursor_) / width)
      throw FormatError("invalid battle environment table count");
    return size_t(count);
  }
  size_t Position() const { return cursor_; }
  std::u16string String() {
    PayloadReader reader(bytes_.subspan(cursor_));auto value=reader.String();
    Skip(bytes_.size()-cursor_-reader.Remaining());return value;
  }

 private:
  std::span<const uint8_t> bytes_;
  size_t cursor_ = 0;
};
}  // namespace

void ScriptLinearTween::Start(int32_t from, int32_t to, int32_t duration,
                              uint64_t now) {
  if (duration < 0) throw FormatError("negative gauge transition duration");
  StartUnsigned(from, to, uint32_t(duration), now);
}
void ScriptLinearTween::StartUnsigned(int32_t from, int32_t to,
                                      uint32_t duration, uint64_t now) {
  From = Current = from;
  To = to;
  Duration = duration;
  Epoch = Sample = now;
}
void ScriptLinearTween::Tick(uint64_t now) {
  if (now < Sample) Epoch = now;
  const auto elapsed = now - Epoch;
  if (Duration == 0 || elapsed >= uint64_t(Duration)) {
    Sample = Epoch + uint64_t(Duration);
    Current = To;
  } else {
    Sample = now;
    const auto delta = Add(To, Negate(From));
    // Native compares the zero-extended 64-bit deadline, but its division
    // helper interprets the low duration word as signed32. Widen division to
    // avoid C++ overflow for INT_MIN/-1, then keep the native low32 result.
    const auto divisor = std::bit_cast<int32_t>(Duration);
    const auto quotient = int64_t(Multiply(delta, int32_t(elapsed))) / divisor;
    Current = Add(From, std::bit_cast<int32_t>(uint32_t(quotient)));
  }
}
bool ScriptLinearTween::Finished() const {
  return Current == To && Sample - Epoch >= uint64_t(Duration);
}
void ScriptReversingTween::Configure(int32_t from,int32_t to,int32_t duration,bool enabled,uint64_t now){
  Channel.Start(from,to,duration,now);Enabled=enabled;Reversals=0;
}
void ScriptReversingTween::Restart(uint64_t now){
  Channel.StartUnsigned(Channel.From,Channel.To,Channel.Duration,now);Enabled=true;
}
void ScriptReversingTween::Tick(uint64_t now){
  if(Enabled)Advance(std::bit_cast<int32_t>(uint32_t(now-Channel.Sample)),now,0);
}
void ScriptReversingTween::Advance(int32_t delta,uint64_t clock,size_t depth){
  if(depth>64)throw UnsupportedCommand("result reverse tween exceeds verified recursion budget");
  auto& c=Channel;
  if(delta<0){delta=0;c.Epoch=c.Sample=clock;}
  const auto sample=c.Sample+uint32_t(delta),deadline=c.Epoch+c.Duration;
  uint32_t carry=0;
  if(sample<deadline){
    c.Sample=sample;
    const auto numerator=Multiply(Add(c.To,Negate(c.From)),std::bit_cast<int32_t>(uint32_t(sample-c.Epoch)));
    const auto divisor=std::bit_cast<int32_t>(c.Duration);
    const auto quotient=int64_t(numerator)/divisor;
    c.Current=Add(c.From,std::bit_cast<int32_t>(uint32_t(quotient)));
  }else{
    carry=uint32_t(sample-deadline);
    if(carry && c.To>0)carry%=uint32_t(c.To);
    c.Sample=deadline;c.Current=c.To;
  }
  if(c.Finished() && Reversals<1){
    c.StartUnsigned(c.To,c.From,c.Duration,clock);
    if(c.Duration)Advance(std::bit_cast<int32_t>(carry),clock,depth+1);
    ++Reversals;
  }
}

static BattleGaugeEnvironment ReadBattleEnvironment(std::span<const uint8_t> bytes,
    BattleVersusEnvironment* versus=nullptr,BattleVersusVisualEnvironment* visual=nullptr,BattleDialogueEnvironment* dialogue=nullptr,
    BattleSelectionEnvironment* selection=nullptr,BattleResultEnvironment* battleResult=nullptr,
    BattleResultVisualEnvironment* resultVisual=nullptr) {
  EnvironmentReader reader(bytes);
  BattleGaugeEnvironment result;
  reader.Skip(3 + 3 * 4);  // flags and colors
  result.Minimum = reader.Word();
  const auto thresholds = reader.Count(4);
  if (thresholds == 0) throw FormatError("battle threshold table is empty");
  result.NeutralThreshold = reader.Word();
  reader.Skip((thresholds - 1) * 4);
  const auto gauge=reader.Sprite();if(versus)versus->Display=gauge.Words[1];
  reader.Skip(2*16);
  for(size_t i=0;i<4;++i){const auto sprite=reader.VisualSprite();if(visual)visual->Sprites[i]=sprite;}
  const auto nameScale=std::bit_cast<float>(reader.Word()),textScale=std::bit_cast<float>(reader.Word());
  if(visual){visual->NameScale=nameScale;visual->TextScale=textScale;}
  const auto points=[&](std::vector<std::array<int32_t,2>>* out){
    const auto count=reader.Count(8);
    for(size_t i=0;i<count;++i){std::array<int32_t,2> point{reader.Word(),reader.Word()};if(out)out->push_back(point);}
  };
  points(visual?&visual->NamePositions:nullptr);points(visual?&visual->NameSizes:nullptr);
  const auto offsets=reader.Count(4);
  for(size_t i=0;i<offsets;++i){const auto word=reader.Word();if(visual)visual->NameOffsets.push_back(word);}
  const auto strings = reader.Count(4);
  for(size_t i=0;i<strings;++i){auto value=reader.String();if(visual)visual->CharacterNames.push_back(std::move(value));}
  points(visual?&visual->TextPositions:nullptr);
  const auto padding=reader.Word();if(visual)visual->TextPadding=padding;
  const auto colors=reader.Count(4);
  for(size_t i=0;i<colors;++i){const auto c=uint32_t(reader.Word());if(visual)visual->Colors.push_back(
      {uint8_t(c>>16),uint8_t(c>>8),uint8_t(c),uint8_t(c>>24)});}
  reader.Skip(4);
  for(size_t i=0;i<2;++i){std::array<int32_t,2> p{reader.Word(),reader.Word()};if(dialogue)dialogue->Positions[i]=p;}
  const auto bubble=reader.VisualSprite();const auto scale=std::bit_cast<float>(reader.Word());
  if(dialogue){dialogue->Bubble=bubble;dialogue->Scale=scale;}
  for(size_t i=0;i<2;++i){std::array<int32_t,2> p{reader.Word(),reader.Word()};if(dialogue)dialogue->LineOffsets[i]=p;}
  const auto textWidth=reader.Word();if(dialogue)dialogue->TextWidth=textWidth;
  if(selection)selection->TextWidth=textWidth;
  reader.Skip(5*34);
  const auto groups = reader.Count(4);
  for (size_t i = 0; i < groups; ++i) reader.Table(8);
  reader.Skip(2 * 34 + 4 + 34 + 3 * 4);
  reader.Table(4);
  reader.Skip(34 + 2 * 8 + 3 * 34);
  const auto resultSprite=[&](size_t i){auto s=reader.VisualSprite();if(resultVisual)resultVisual->Sprites[i]=s;};
  const auto resultColor=[&](std::array<uint8_t,4>* out){const auto c=uint32_t(reader.Word());
    if(out)*out={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c),uint8_t(c>>24)};};
  const auto rectangles=[&](std::vector<std::array<int32_t,4>>* out){const auto count=reader.Count(16);
    for(size_t i=0;i<count;++i){std::array<int32_t,4> r;for(auto& word:r)word=reader.Word();if(out)out->push_back(r);}};
  resultSprite(0);resultSprite(1);resultColor(resultVisual?&resultVisual->GlyphColor:nullptr);
  rectangles(resultVisual?&resultVisual->EqualRectangles:nullptr);
  points(resultVisual?&resultVisual->EqualPositions:nullptr);
  resultSprite(2);resultSprite(3);
  resultColor(resultVisual?&resultVisual->UpperFrameColor:nullptr);resultColor(resultVisual?&resultVisual->LowerFrameColor:nullptr);
  rectangles(resultVisual?&resultVisual->UpperRectangles:nullptr);rectangles(resultVisual?&resultVisual->LowerRectangles:nullptr);
  points(resultVisual?&resultVisual->UpperPositions:nullptr);points(resultVisual?&resultVisual->LowerPositions:nullptr);
  points(resultVisual?&resultVisual->UpperBases:nullptr);points(resultVisual?&resultVisual->LowerBases:nullptr);
  std::array<int32_t,13> settings;for(auto& word:settings)word=reader.Word();
  if(battleResult){battleResult->ItemAmplitude=settings[0];battleResult->ItemDuration=settings[1];
    battleResult->RevealDuration=settings[6];battleResult->FadeDuration=settings[12];}
  if(resultVisual)resultVisual->UpperGlyphScale=std::bit_cast<float>(settings[2]);
  if(selection)selection->RevealDuration=settings[3]; // global80261a74, distinct from card motion.
  if(versus){versus->Offscreen=settings[4];versus->SlideDuration=settings[5];
    versus->FirstDuration=settings[6];versus->SecondDuration=settings[7];
    versus->TextDuration=settings[8];versus->TextTravel=settings[9];
    versus->IconDuration=settings[10];versus->FadeDuration=settings[11];}
  result.Duration = reader.Word();
  result.DisplayPerPoint = reader.Word();
  result.XPerPoint = reader.Word();
  result.ScalePerPoint = std::bit_cast<float>(reader.Word());
  if(battleResult){reader.Word();battleResult->EqualPose=reader.Word();battleResult->AbovePose=reader.Word();battleResult->BelowPose=reader.Word();}
  if(selection) {
    // Original loader801dfc36..801e0b5c. Tables remain counted even when their
    // current authored lengths happen to be three; never seek by file offset.
    reader.Skip(4*4); // Other gauge/effect fields80261aac..80261ab8.
    points(&selection->Positions);
    const auto sprites=reader.Count(34);
    for(size_t i=0;i<sprites;++i)selection->CardSprites.push_back(reader.VisualSprite());
    selection->CardScale=std::bit_cast<float>(reader.Word());
    for(auto& p:selection->LineOffsets)p={reader.Word(),reader.Word()};
    for(auto& p:selection->Pulses) {
      p.From=std::bit_cast<float>(reader.Word());p.To=std::bit_cast<float>(reader.Word());p.Duration=reader.Word();
    }
    const auto labels=reader.Count(4);
    for(size_t i=0;i<labels;++i)selection->Labels.push_back(reader.String());
    points(&selection->LabelPositions);
    selection->LabelScale=std::bit_cast<float>(reader.Word());
    const auto colors=[&](auto& out) {
      const auto count=reader.Count(4);
      for(size_t i=0;i<count;++i){const auto c=uint32_t(reader.Word());
        out.push_back({uint8_t(c>>16),uint8_t(c>>8),uint8_t(c),uint8_t(c>>24)});}
    };
    colors(selection->FillColors);colors(selection->FrameColors);
    // Following three sprites, point, clip rectangle and two words belong to
    // the separate HUD. Consume structurally to reach the selection timings.
    reader.Skip(3*34+8+16+2*4);
    selection->MotionDuration=reader.Word();selection->Travel=reader.Word();selection->FadeDuration=reader.Word();
  }
  if (result.Minimum < 0 || result.Minimum > 50 || result.Duration < 0 ||
      !std::isfinite(result.ScalePerPoint))
    throw FormatError("invalid battle gauge environment");
  return result;
}
BattleGaugeEnvironment BattleGaugeEnvironment::Decode(std::span<const uint8_t> bytes) {
  return ReadBattleEnvironment(bytes);
}
BattleResultEnvironment BattleResultEnvironment::Decode(std::span<const uint8_t> bytes){
  BattleResultEnvironment out;ReadBattleEnvironment(bytes,nullptr,nullptr,nullptr,nullptr,&out);
  if(out.ItemDuration<0 || out.ItemDuration>INT32_MAX/2 || out.RevealDuration<0 || out.FadeDuration<0)
    throw FormatError("invalid battle result environment timing");
  return out;
}
BattleResultVisualEnvironment BattleResultVisualEnvironment::Decode(std::span<const uint8_t> bytes){
  BattleResultVisualEnvironment out;ReadBattleEnvironment(bytes,nullptr,nullptr,nullptr,nullptr,nullptr,&out);
  if(!std::isfinite(out.UpperGlyphScale))throw FormatError("invalid battle result glyph scale");
  if(out.EqualPositions.size()>4 || out.UpperPositions.size()>4 ||
     out.EqualRectangles.size()<out.EqualPositions.size() || out.UpperRectangles.size()<out.UpperPositions.size() ||
     out.LowerPositions.size()<out.UpperPositions.size() || out.LowerRectangles.size()<out.UpperPositions.size() ||
     (!out.UpperPositions.empty() && (out.UpperBases.size()<2 || out.LowerBases.size()<2)))
    throw FormatError("battle result geometry exceeds owning animation/table bounds");
  return out;
}
BattleVersusEnvironment BattleVersusEnvironment::Decode(std::span<const uint8_t> bytes) {
  BattleVersusEnvironment out;ReadBattleEnvironment(bytes,&out);return out;
}
BattleVersusVisualEnvironment BattleVersusVisualEnvironment::Decode(std::span<const uint8_t> bytes) {
  BattleVersusVisualEnvironment out;ReadBattleEnvironment(bytes,nullptr,&out);
  if(!std::isfinite(out.NameScale) || !std::isfinite(out.TextScale) || out.NameScale<0 || out.TextScale<0)
    throw FormatError("invalid battle versus text scale");
  return out;
}
BattleDialogueEnvironment BattleDialogueEnvironment::Decode(std::span<const uint8_t> bytes) {
  BattleDialogueEnvironment out;ReadBattleEnvironment(bytes,nullptr,nullptr,&out);
  if(!std::isfinite(out.Scale) || out.TextWidth<=0)throw FormatError("invalid battle dialogue environment");
  return out;
}
BattleSelectionEnvironment BattleSelectionEnvironment::Decode(std::span<const uint8_t> bytes) {
  BattleSelectionEnvironment out;ReadBattleEnvironment(bytes,nullptr,nullptr,nullptr,&out);
  if(out.RevealDuration<0 || out.MotionDuration<0 || out.FadeDuration<0 ||
      !std::isfinite(out.CardScale) || !std::isfinite(out.LabelScale))
    throw FormatError("invalid battle selection environment");
  for(const auto& p:out.Pulses)
    if(!std::isfinite(p.From) || !std::isfinite(p.To) || p.Duration<0)
      throw FormatError("invalid battle selection pulse");
  return out;
}
BattleGaugeCommand BattleGaugeCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  BattleGaugeCommand result{reader.I32(), reader.I32()};
  // The loader consumes exactly eight bytes. Record size governs the walker;
  // native records also contain an unread alignment tail (two bytes here).
  return result;
}

void StartBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                      const BattleGaugeCommand& command,
                      const BattleGaugeEnvironment& environment,
                      Live2dDriver* models, uint64_t now) {
  if (!state.Battle.Known)
    throw UnsupportedCommand("restored battle display/side context is missing");
  if (!state.Live2d.Known)
    throw UnsupportedCommand("restored battle model records are missing");
  if (environment.Minimum < 0 || environment.Minimum > 50 ||
      environment.Duration < 0 || !std::isfinite(environment.ScalePerPoint))
    throw FormatError("invalid battle gauge environment");
  auto target = state.BattleGauge;
  const bool side0 = command.Character == state.Battle.SideCharacters[0];
  auto delta = side0 ? command.Delta : Negate(command.Delta);
  target = Add(target, delta);
  // Intentionally directional. Negative authored requests do not imply a
  // symmetric clamp; both native Start branches only clamp their own end.
  if (side0 && target > 100 - environment.Minimum) {
    delta = Add(delta, Add(100 - environment.Minimum, Negate(target)));
    target = 100 - environment.Minimum;
  } else if (!side0 && target < environment.Minimum) {
    delta = Add(delta, Add(environment.Minimum, Negate(target)));
    target = environment.Minimum;
  }
  BattleGaugeAnimation next;
  next.Started = true;
  next.ActualDelta = delta;
  next.Display.Start(
      state.Battle.Display,
      Add(state.Battle.Display, Multiply(delta, environment.DisplayPerPoint)),
      environment.Duration, now);
  for (size_t side = 0; side < 2; ++side) {
    const auto* record = ActorRecord(state, side);
    if (!record || !record->ResourceId) continue;
    if (!models)
      throw UnsupportedCommand("battle model transform backend is unavailable");
    const auto transform = models->CurrentTransform(*record);
    BattleGaugeAnimation::Actor actor;
    const float x = transform.Position[0];
    const float scale = transform.Scale[0] * 100.0f;
    const float scaleDelta = float(delta) * environment.ScalePerPoint;
    actor.X.Start(Truncate(x),
                  Truncate(x + float(Multiply(delta, environment.XPerPoint))),
                  environment.Duration, now);
    actor.Scale.Start(
        Truncate(scale),
        Truncate(side == 0 ? scale + scaleDelta : scale - scaleDelta),
        environment.Duration, now);
    actor.Y.Start(0, 0, environment.Duration, now);
    actor.BaselineY = Truncate(transform.Position[1]);
    next.Actors[side] = actor;
  }
  // Resolve/validate all inputs before committing Start's logical score change.
  animation = next;
  state.BattleGauge = target;
  const int32_t standing = target < environment.NeutralThreshold   ? 0
                           : target > environment.NeutralThreshold ? 2
                                                                   : 1;
  state.Battle.Standings = {standing, 2 - standing};
}
bool UpdateBattleGauge(SemanticState& state, BattleGaugeAnimation& animation,
                       Live2dDriver* models, bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand(
        "battle gauge Start was suppressed; animation state is unavailable");
  animation.Display.Tick(now);
  state.Battle.Display = animation.Display.Current;
  if (!skipping) {
    for (size_t side = 0; side < 2; ++side) {
      if (!animation.Actors[side]) continue;
      auto& actor = *animation.Actors[side];
      actor.X.Tick(now);
      actor.Scale.Tick(now);
      actor.Y.Tick(now);
      const auto* record = ActorRecord(state, side);
      if (!record || !record->ResourceId) continue;
      if (!models)
        throw UnsupportedCommand(
            "battle model transform backend is unavailable");
      const float scale = float(actor.Scale.Current) / 100.0f;
      // Guard both sides independently. Native8007f604 dereferences side1 under
      // a side0-only guard; retain intended behavior without that null access.
      models->SetTransform(*record,
                           {{float(actor.X.Current),
                             float(Add(actor.BaselineY, actor.Y.Current))},
                            {scale, scale}},
                           now);
    }
  }
  return animation.Display.Finished();
}

namespace {
ApproachGaugeEnvironment ReadApproachGauge(
    EnvironmentReader& reader, ModeResultVisualEnvironment* visuals = nullptr,
    ApproachMissionEnvironment* mission = nullptr,
    ApproachVisualEnvironment* approachVisuals = nullptr,
    ApproachSelectionEnvironment* selection = nullptr,
    ApproachSelectionLayout* layout = nullptr) {
  ApproachGaugeEnvironment result;
  reader.Skip(3 + 3 * 4);
  for (size_t i = 0; i < 6; ++i) {
    const auto sprite = reader.Sprite();
    if (approachVisuals) {
      const auto color=uint32_t(sprite.Words[7]);
      approachVisuals->Sprites[i]={sprite.Resource,sprite.Words[0],sprite.Words[1],
          sprite.Words[2],sprite.Words[3],sprite.Words[4],sprite.Words[5],sprite.Words[6],
          {uint8_t(color>>16),uint8_t(color>>8),uint8_t(color),uint8_t(color>>24)}};
    }
    if(mission && i==1){mission->LeftX=sprite.Words[1];mission->LeftWidth=sprite.Words[5];}
    if(mission && i==2)mission->RightX=sprite.Words[1];
    if (i == 0 && visuals) {
      const auto color = uint32_t(sprite.Words[7]);
      visuals->Banner = {sprite.Resource, sprite.Words[0], sprite.Words[1],
                         sprite.Words[2], sprite.Words[3], sprite.Words[4],
                         sprite.Words[5], sprite.Words[6],
                         {uint8_t(color >> 16), uint8_t(color >> 8),
                          uint8_t(color), uint8_t(color >> 24)}};
    }
    if (i == 3)
      result.ParticleRegion = {sprite.Words[1], sprite.Words[2],
                               sprite.Words[5], sprite.Words[6]};
    if (i == 5) result.ParticleSprite = sprite;
  }
  result.PositiveEffectPosition = {reader.Word(), reader.Word()};
  result.NegativeEffectPosition = {reader.Word(), reader.Word()};
  if (approachVisuals) {
    const auto count=reader.Count(34);
    approachVisuals->CharacterLabels.reserve(count);
    for(size_t i=0;i<count;++i)approachVisuals->CharacterLabels.push_back(reader.VisualSprite());
  } else reader.Table(34);
  if(layout) {
    layout->Sprites[0]=reader.VisualSprite();
    layout->Sprites[1]=reader.VisualSprite();
    for(auto& value:layout->TextOffset)value=reader.Word();
    layout->Sprites[2]=reader.VisualSprite();
    for(auto& value:layout->HitRectangle)value=reader.Word();
    for(auto& table:layout->ArrowSprites) {
      const auto count=reader.Count(34);table.reserve(count);
      for(size_t i=0;i<count;++i)table.push_back(reader.VisualSprite());
    }
    const auto count=reader.Count(16);layout->ArrowHitRectangles.reserve(count);
    for(size_t i=0;i<count;++i) {
      std::array<int32_t,4> rectangle{};
      for(auto& value:rectangle)value=reader.Word();
      layout->ArrowHitRectangles.push_back(rectangle);
    }
    const auto points=[&](size_t index) {
      const auto count=reader.Count(8);auto& table=layout->PointTables[index];table.reserve(count);
      for(size_t i=0;i<count;++i)table.push_back({reader.Word(),reader.Word()});
    };
    points(0);points(1);layout->SelectedOffset=reader.Word();
    for(size_t i=2;i<6;++i)points(i);
    const auto color=[&]() {
      const auto value=uint32_t(reader.Word());return std::array<uint8_t,4>{
        uint8_t(value>>16),uint8_t(value>>8),uint8_t(value),uint8_t(value>>24)};
    };
    layout->Colors[0]=color();layout->TextScale=std::bit_cast<float>(uint32_t(reader.Word()));
    layout->Colors[1]=color();layout->Colors[2]=color();
    if(!std::isfinite(layout->TextScale) || layout->TextScale<0)
      throw FormatError("invalid approach selection text scale");
    for(auto& rectangle:layout->Rectangles)for(auto& value:rectangle)value=reader.Word();
    for(auto& value:layout->Offset)value=reader.Word();
  } else {
    reader.Skip(2 * 34 + 8 + 34 + 16);
    reader.Table(34);reader.Table(34);reader.Table(16);
    reader.Table(8);reader.Table(8);reader.Skip(4);
    for(size_t i=0;i<4;++i)reader.Table(8);
    reader.Skip(4*4+2*16+2*4);
  }
  if(selection) {
    selection->ScrollDistance=reader.Word();selection->ScrollDuration=reader.Word();
    if(selection->ScrollDistance<0 || selection->ScrollDuration<0)
      throw FormatError("invalid approach selection scroll configuration");
  } else reader.Skip(2*4);
  if(layout)layout->PrefixBytes=reader.Position();
  if (visuals) {
    visuals->ProgressFill = reader.VisualSprite();  // bc18
    visuals->Frame = reader.VisualSprite();  // bc3c
    visuals->MaskedContent = reader.VisualSprite();  // bc60
    visuals->Mask = reader.VisualSprite();  // bc84
    visuals->TextPosition = {reader.Word(), reader.Word()};  // bca8
    visuals->TextWidth = reader.Word();  // bcb0
    visuals->TextScale = std::bit_cast<float>(uint32_t(reader.Word()));  // bcb4
    visuals->EventIcon = reader.VisualSprite();  // bcb8
    visuals->Outcomes[1] = reader.VisualSprite();  // bcdc
    reader.Skip(34);  // bd00, unused by this draw pass.
    visuals->Outcomes[2] = reader.VisualSprite();  // bd24
    visuals->Outcomes[0] = reader.VisualSprite();  // bd48
    reader.Skip(34);  // bd6c
  } else {
    reader.Skip(4 * 34 + 4 * 4 + 6 * 34);
  }
  result.Duration = reader.Word();
  result.ParticleMinimumDuration = reader.Word();
  result.ParticleMaximumDuration = reader.Word();
  const auto count = reader.Word();
  if (result.Duration < 0 || result.ParticleMinimumDuration <= 0 ||
      result.ParticleMaximumDuration < result.ParticleMinimumDuration ||
      count < 0 || count > 10000 || result.ParticleRegion[2] < 0 ||
      result.ParticleRegion[3] < 0)
    throw FormatError("invalid approach gauge environment");
  result.ParticleCount = size_t(count);
  return result;
}
}  // namespace
ApproachGaugeEnvironment ApproachGaugeEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  return ReadApproachGauge(reader);
}
ApproachVisualEnvironment ApproachVisualEnvironment::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachVisualEnvironment result;
  ReadApproachGauge(reader,nullptr,nullptr,&result);
  return result;
}
ApproachSelectionEnvironment ApproachSelectionEnvironment::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachSelectionEnvironment result;
  ReadApproachGauge(reader,nullptr,nullptr,nullptr,&result);return result;
}
ApproachSelectionLayout ApproachSelectionLayout::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachSelectionLayout result;
  ReadApproachGauge(reader,nullptr,nullptr,nullptr,nullptr,&result);return result;
}
ApproachMissionEnvironment ApproachMissionEnvironment::Decode(std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);ApproachMissionEnvironment result;
  ReadApproachGauge(reader,nullptr,&result);
  result.EntranceAlpha=reader.Word();result.LeftSlide=reader.Word();result.RightSlide=reader.Word();
  result.Hold=reader.Word();result.Exit=reader.Word();result.ExitDistance=reader.Word();
  result.PrefixBytes=reader.Position();
  if(result.LeftWidth<0 || result.EntranceAlpha<0 || result.LeftSlide<0 ||
      result.RightSlide<0 || result.Hold<0 || result.Exit<0)
    throw FormatError("invalid approach mission environment");
  return result;
}
ModeResultEnvironment ModeResultEnvironment::DecodeApproach(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  ReadApproachGauge(reader);
  // Native801c9692..801c9c8c: six prior scalar settings, then nine result
  // fields. Walk the variable gauge tables above instead of byte1289.
  reader.Skip(6 * 4);
  ModeResultEnvironment result;
  result.Progress = reader.Word();
  result.HorizontalSlide = reader.Word();
  result.HorizontalStart = reader.Word();
  result.HorizontalOpacity = reader.Word();
  result.VerticalSlide = reader.Word();
  result.VerticalStart = reader.Word();
  result.VerticalOpacity = reader.Word();
  result.SecondaryProgress = reader.Word();
  result.TextOpacity = reader.Word();
  result.PrefixBytes = reader.Position();
  for (const auto duration :
       {result.Progress, result.HorizontalSlide, result.HorizontalOpacity,
        result.VerticalSlide, result.VerticalOpacity, result.SecondaryProgress,
        result.TextOpacity})
    if (duration < 0) throw FormatError("negative mode result duration");
  return result;
}
ModeResultVisualEnvironment ModeResultVisualEnvironment::DecodeApproach(
    std::span<const uint8_t> bytes) {
  EnvironmentReader reader(bytes);
  ModeResultVisualEnvironment result;
  ReadApproachGauge(reader, &result);
  reader.Skip((6 + 9) * 4);  // Timings.
  for (auto& position : result.CursorPositions)
    position={reader.Word(),reader.Word()};  // bddc/bde4.
  reader.Skip(4);  // bdec.
  result.OverlayAlpha = reader.Word();  // bdf0
  const auto colors = reader.Count(4);
  for (size_t i = 0; i < colors; ++i) {
    const auto color = uint32_t(reader.Word());
    result.CharacterColors.push_back({uint8_t(color >> 16), uint8_t(color >> 8),
                                      uint8_t(color), uint8_t(color >> 24)});
  }
  result.PrefixBytes = reader.Position();
  return result;
}
ApproachGaugeCommand ApproachGaugeCommand::Decode(
    std::span<const uint8_t> payload) {
  PayloadReader reader(payload);
  return {reader.I32()};  // Remaining record alignment bytes are not fields.
}
namespace {
void StartParticle(ApproachParticle& particle, GameRandom& random, size_t index,
                   const ApproachGaugeEnvironment& environment, uint64_t now) {
  const auto duration = random.Range(environment.ParticleMinimumDuration,
                                     environment.ParticleMaximumDuration);
  particle.Opacity.Start(0, 100, duration, now);
  const auto width = environment.ParticleRegion[2] / 3;
  const auto height = environment.ParticleRegion[3] / 3;
  const auto x =
      Add(environment.ParticleRegion[0], Multiply(width, int32_t(index % 3)));
  const auto y =
      Add(environment.ParticleRegion[1], Multiply(height, int32_t(index / 3)));
  particle.Position = {float(random.Range(x, Add(x, width))),
                       float(random.Range(y, Add(y, height)))};
}
void TickParticle(ApproachParticle& particle, GameRandom& random, size_t index,
                  const ApproachGaugeEnvironment& environment, uint64_t now) {
  if (!particle.Position)
    throw UnsupportedCommand("approach particle runtime is missing");
  particle.Opacity.Tick(now);
  if (!particle.Opacity.Finished()) return;
  if (particle.Opacity.To != 0)
    particle.Opacity.Start(particle.Opacity.To, particle.Opacity.From,
                           particle.Opacity.Duration, now);
  else
    StartParticle(particle, random, index, environment, now);
}
void ApproachCompletionEvents(SemanticState& state, int32_t delta) {
  if (state.ApproachGauge != 100) return;
  if (delta != 0) state.PendingEvents.push_back({GameEventKind::Sound, 23});
  state.PendingEvents.push_back({GameEventKind::Trophy, 37});
}
}  // namespace
void StartApproachGauge(SemanticState& state, ApproachGaugeAnimation& animation,
                        ApproachGaugeCommand& command,
                        const ApproachGaugeEnvironment& environment,
                        uint64_t now) {
  if (!state.Approach.Known)
    throw UnsupportedCommand("restored approach particle context is missing");
  if (!state.Approach.Particles.empty() && !state.Random.Known())
    throw UnsupportedCommand("restored game random state is missing");
  if (state.Approach.Particles.size() != environment.ParticleCount)
    throw UnsupportedCommand(
        "restored approach particle count differs from native configuration");
  if (state.ApproachGauge < 0 || state.ApproachGauge > 100)
    throw FormatError("initial approach gauge is outside0..100");
  if (environment.Duration < 0 || environment.ParticleMinimumDuration <= 0 ||
      environment.ParticleMaximumDuration <
          environment.ParticleMinimumDuration ||
      environment.ParticleRegion[2] < 0 || environment.ParticleRegion[3] < 0)
    throw FormatError("invalid approach gauge environment");
  for (size_t i = 0; i < state.Approach.Particles.size(); ++i)
    StartParticle(state.Approach.Particles[i], state.Random, i, environment,
                  now);
  animation = {};
  animation.Started = true;
  animation.Initial = state.ApproachGauge;
  animation.Target = int32_t(std::clamp(
      int64_t(animation.Initial) + command.Delta, int64_t(0), int64_t(100)));
  command.Delta = animation.Target - animation.Initial;
  animation.Progress.Start(0, 100, environment.Duration, now);
  state.Approach.Changing = true;
  if (command.Delta != 0) {
    const bool positive = command.Delta > 0;
    state.PendingEvents.push_back({GameEventKind::Effect, positive ? 10 : 11,
                                   positive
                                       ? environment.PositiveEffectPosition
                                       : environment.NegativeEffectPosition});
    state.PendingEvents.push_back({GameEventKind::Sound, positive ? 21 : 22});
  }
}
bool UpdateApproachGauge(SemanticState& state,
                         ApproachGaugeAnimation& animation,
                         const ApproachGaugeCommand& command,
                         const ApproachGaugeEnvironment& environment,
                         bool skipping, uint64_t now) {
  if (!animation.Started)
    throw UnsupportedCommand(
        "approach gauge Start was suppressed; animation state is unavailable");
  if (animation.Completed) return true;
  if (skipping) {
    // Intended endpoint: native adds the entire delta again to an already
    // interpolated gauge. Snap to the original clamped target instead, so a
    // normal->skip transition cannot overshoot or count progress twice.
    state.ApproachGauge = animation.Target;
    animation.Completed = true;
  } else {
    for (size_t i = 0; i < state.Approach.Particles.size(); ++i)
      TickParticle(state.Approach.Particles[i], state.Random, i, environment,
                   now);
    animation.Progress.Tick(now);
    if (animation.Progress.Finished()) {
      state.ApproachGauge = animation.Target;
      state.Approach.Changing = false;
      animation.Completed = true;
    } else {
      const float scaled =
          float(command.Delta) * float(animation.Progress.Current) / 100.0f;
      state.ApproachGauge = animation.Initial + static_cast<int32_t>(scaled);
    }
  }
  if (animation.Completed) ApproachCompletionEvents(state, command.Delta);
  return animation.Completed;
}
}  // namespace Impacto::Saekano
