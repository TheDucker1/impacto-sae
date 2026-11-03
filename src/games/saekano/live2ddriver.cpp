#include "live2ddriver.h"

#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
namespace {
constexpr std::array<Live2dSection, 5> MotionSections{
    Live2dSection::LoopMotions, Live2dSection::OneShotMotions,
    Live2dSection::AngerMotions, Live2dSection::SweatMotions,
    Live2dSection::TearMotions};
int32_t Percent(float value) {
  const auto scaled = value * 100.0f;
  if (!std::isfinite(scaled) ||
      double(scaled) < std::numeric_limits<int32_t>::min() ||
      double(scaled) > std::numeric_limits<int32_t>::max())
    throw FormatError("Live2D percentage exceeds integer range");
  return static_cast<int32_t>(scaled);  // ARM vcvt.s32.f32 truncates.
}
size_t Ordinal(int32_t index, size_t count) {
  if (index < 0 || size_t(index) >= count)
    throw FormatError("Live2D asset index out of range");
  return size_t(index);
}
}  // namespace

struct SdkLive2dDriver::Item {
  Live2dModelAssets Assets;
  std::unique_ptr<OpenGL::Cubism2Model> Model;
  std::array<std::vector<size_t>, 5> Motions;
  std::vector<size_t> Expressions;
  bool Attached = false, Enabled = false;
  std::array<int32_t, 2> Anchor{};
  std::array<Live2dFrameAxis, 2> Position;
  std::array<Live2dFrameAxis, 2> Scale;
  std::array<uint8_t, 4> Color{255, 255, 255, 255};
  std::optional<size_t> Loop, QueuedLoop, PendingOneShot, Anger, Tear,
      Expression;
  int OneShotHandle = -1, AngerHandle = -1, TearHandle = -1;
  bool OneShotActive = false, AngerRamp = false, SweatRamp = false;
  bool TearStopping = false, FirstExpression = true;
  std::array<Live2dIntegerTween, 4> Eyes;
  std::array<int32_t, 4> EyeTargets{};
  std::array<Live2dIntegerTween, 2> AngerTweens;
  Live2dIntegerTween SweatTween;
  bool MouthActive = false;
  uint64_t MouthEpoch = 0;
  float Mouth = 0;
  bool MouthEnvelopeBound=false;
  ModelMouthEnvelope Envelope;

  Item(OpenGL::Cubism2Sdk& sdk, Live2dModelAssets assets) : Assets(std::move(assets)) {
    const auto& mocs = Assets.Get(Live2dSection::Moc);
    if (mocs.size() != 1) throw FormatError("expected one Live2D MOC");
    Model =
        sdk.LoadModel(mocs[0].Data, Assets.Get(Live2dSection::Textures).size());
    const auto& poses = Assets.Get(Live2dSection::Pose);
    if (poses.size() != 1) throw FormatError("expected one Live2D pose file");
    Model->LoadPose(poses[0].Data);
    for (size_t section = 0; section < MotionSections.size(); ++section)
      for (const auto& asset : Assets.Get(MotionSections[section]))
        Motions[section].push_back(Model->LoadMotion(asset.Data, false));
    for (const auto& asset : Assets.Get(Live2dSection::Expressions))
      Expressions.push_back(Model->LoadExpression(asset.Data));
    for (auto& axis : Scale) axis.Target(1, 0);
  }
  size_t Motion(size_t section, int32_t index) const {
    const auto& list = Motions.at(section);
    return list.at(Ordinal(index, list.size()));
  }
  void SelectExpression(int32_t index, uint64_t now) {
    const auto expression = Expressions.at(Ordinal(index, Expressions.size()));
    const auto targets = Model->ExpressionEyeTargets(expression);
    for (size_t eye = 0; eye < Eyes.size(); ++eye) {
      const auto next = Percent(targets[eye]);
      Eyes[eye].Start(FirstExpression ? next : EyeTargets[eye], next, 200, now);
      EyeTargets[eye] = next;
    }
    FirstExpression = false;
    Expression = size_t(index);
    Model->StartExpression(expression);
  }
  void Animate(uint64_t now, const Live2dEnvironment& environment) {
    auto& model = *Model;
    model.RestoreParameters();
    if (PendingOneShot) {
      if (model.Parameter("PARAM_EYE_L_OPEN") > 0.99f &&
          model.Parameter("PARAM_EYE_R_OPEN") > 0.99f) {
        OneShotHandle = model.StartMotion(*PendingOneShot, 0);
        OneShotActive = true;
      }
      // Native drops a request when the eye gate fails; it does not retry.
      PendingOneShot.reset();
    }
    if (OneShotActive && model.MotionFinished(0, OneShotHandle)) {
      OneShotActive = false;
      if (QueuedLoop) {
        Loop = QueuedLoop;
        QueuedLoop.reset();
      }
      if (Loop) model.StartMotion(*Loop, 0);
    }
    model.ApplyMotionChannel(0);
    model.ApplyMotionChannel(1);
    if (Anger) {
      if (AngerRamp) {
        constexpr std::array<const char*, 2> params{"PARAM_ANGER_1",
                                                    "PARAM_?_AP"};
        if (!AngerTweens[0].Enabled)
          for (size_t i = 0; i < 2; ++i)
            AngerTweens[i].Start(Percent(model.Parameter(params[i])), 0, 500,
                                 now);
        for (auto& tween : AngerTweens) tween.Tick(now);
        if (AngerTweens[0].Finished() && AngerTweens[1].Finished())
          model.StopMotionChannel(1);
        for (size_t i = 0; i < 2; ++i)
          model.SetParameter(params[i], float(AngerTweens[i].Current) / 100);
      }
      if (model.MotionFinished(1, AngerHandle))
        AngerHandle = model.StartMotion(*Anger, 1);
    }
    model.ApplyMotionChannel(2);
    if (SweatRamp) {
      SweatTween.Tick(now);
      model.SetParameter("PARAM_SWEAT", float(SweatTween.Current) / 100);
      if (SweatTween.Finished()) {
        model.StopMotionChannel(2);
        SweatRamp = false;
        model.SetParameter("PARAM_SWEAT", 0);
      }
    }
    model.ApplyMotionChannel(3);
    if (Tear && model.MotionFinished(3, TearHandle)) {
      if (TearStopping) {
        Tear.reset();
        model.SetParameter("PARAM_EYE_URU", 0);
      } else
        TearHandle = model.StartMotion(*Tear, 3);
    }
    if (!OneShotActive && Loop) model.ApplyBlink();
    const auto left = model.Parameter("PARAM_EYE_L_OPEN");
    const auto right = model.Parameter("PARAM_EYE_R_OPEN");
    if(MouthEnvelopeBound) {
      Envelope.Update(now);MouthActive=Envelope.Active;MouthEpoch=Envelope.ActiveEpoch;
    }
    Mouth = 0;
    if (MouthActive) {
      if (now < MouthEpoch) MouthEpoch = now;
      const uint64_t open = uint64_t(environment.MouthCycleParts[0]);
      const uint64_t close = uint64_t(environment.MouthCycleParts[1]);
      const auto phase = (now - MouthEpoch) % (open + close);
      Mouth = phase > open ? float(open + close - phase) / float(close)
                           : float(phase) / float(open);
    }
    model.SetParameter("PARAM_MOUTH_OPEN_Y", Mouth);
    model.SaveParameters();
    // The other two procedural queues start empty; selectors that activate
    // them are not supported. Authored 0x438 commands use these four MTN
    // queues.
    model.ApplyExpression();
    const auto pi = std::bit_cast<double>(uint64_t(0x400921cac0000000));
    const auto period = std::bit_cast<double>(uint64_t(0x402911a9fbe76c8b));
    model.AddParameter(
        "PARAM_SMILE",
        float(std::sin(double(now) / 1000 * 2 * pi / period) * 0.1));
    if (Expression) {
      for (auto& tween : Eyes) tween.Tick(now);
      // Native consumer 800931e4 uses the left tween for BOTH eyes. The other
      // three tweens still advance; retain this verified game-side behavior.
      const float factor = float(Eyes[0].Current) / 100;
      model.SetParameter("PARAM_EYE_L_OPEN", left * factor);
      model.SetParameter("PARAM_EYE_R_OPEN", right * factor);
    }
  }
};

SdkLive2dDriver::SdkLive2dDriver(GameResources& resources,
                                 Live2dEnvironment environment)
    : resources_(resources), environment_(std::move(environment)) {}
SdkLive2dDriver::~SdkLive2dDriver() = default;
Live2dTransform SdkLive2dDriver::CurrentTransform(const Live2dRecord& record) const {
  if (!record.ResourceId) throw UnsupportedCommand("battle model record has no resource ID");
  const auto& item = Get(*record.ResourceId);
  if (!item.Attached) throw UnsupportedCommand("battle model resource is not attached");
  return {{item.Position[0].Current, item.Position[1].Current},
          {item.Scale[0].Current, item.Scale[1].Current}};
}
void SdkLive2dDriver::SetTransform(const Live2dRecord& record,
                                  const Live2dTransform& transform, uint64_t now) {
  for (auto value : transform.Position)
    if (!std::isfinite(value)) throw FormatError("non-finite battle position");
  for (auto value : transform.Scale)
    if (!std::isfinite(value)) throw FormatError("non-finite battle scale");
  auto& item = Get(record);
  sdk_.SetTime(now);
  for (size_t axis = 0; axis < 2; ++axis) {
    item.Position[axis].Target(transform.Position[axis], 0);
    item.Scale[axis].Target(transform.Scale[axis], 0);
  }
}
bool SdkLive2dDriver::Supports(uint32_t selector) const {
  switch (selector) {
    case 2:
    case 3:
    case 4:
    case 6:
    case 12:
    case 13:
    case 15:
    case 16:
    case 17:
    case 19:
    case 21:
    case 23:
    case 24:
    case 25:
      return true;
    default:
      return false;
  }
}
bool SdkLive2dDriver::ResourceReady(int32_t resource) const {
  return items_.contains(resource);
}
SdkLive2dDriver::Item& SdkLive2dDriver::Get(int32_t resource) {
  const auto found = items_.find(resource);
  if (found == items_.end())
    throw UnsupportedCommand("Live2D SDK resource is not loaded");
  return *found->second;
}
const SdkLive2dDriver::Item& SdkLive2dDriver::Get(int32_t resource) const {
  const auto found = items_.find(resource);
  if (found == items_.end())
    throw UnsupportedCommand("Live2D SDK resource is not loaded");
  return *found->second;
}
SdkLive2dDriver::Item& SdkLive2dDriver::Get(const Live2dRecord& record) {
  if (!record.ResourceId)
    throw UnsupportedCommand("Live2D record has no SDK resource");
  auto& item = Get(*record.ResourceId);
  if (!item.Attached)
    throw UnsupportedCommand("Live2D SDK resource is not attached");
  return item;
}
void SdkLive2dDriver::RequestResource(int32_t resource, int32_t logical,
                                      uint64_t now) {
  environment_.Actor(logical);
  if (resource < 0 || size_t(resource) >= resources_.Live2dCount())
    throw FormatError("Live2D resource ID out of range");
  if (ResourceReady(resource)) return;
  sdk_.SetTime(now);
  auto item =
      std::make_unique<Item>(sdk_, resources_.LoadLive2d(size_t(resource)));
  items_.emplace(resource, std::move(item));
}
void SdkLive2dDriver::Attach(const Live2dRecord& record, int32_t logical,
                             uint64_t now) {
  if (!record.ResourceId)
    throw UnsupportedCommand("Live2D attach has no resource ID");
  auto& item = Get(*record.ResourceId);
  const auto& config = environment_.Actor(logical);
  sdk_.SetTime(now);
  item.Anchor = config.Anchor;
  item.Model->ConfigureBlink(config.BlinkInterval, config.BlinkMotion);
  item.Attached = true;
}
void SdkLive2dDriver::Apply(const Live2dRecord& record,
                            const Live2dCommand& command, uint64_t now) {
  auto& item = Get(record);
  auto& model = *item.Model;
  sdk_.SetTime(now);
  switch (command.Selector) {
    case 3:
      item.Enabled = command.Value != 0;
      break;
    case 4:
      item.PendingOneShot = item.Motion(1, command.Value);
      model.SetMotionLoop(*item.PendingOneShot, false);
      item.OneShotActive = false;
      break;
    case 6:
      item.SelectExpression(command.Value, now);
      break;
    case 12:
    case 13: {
      auto& axes = command.Selector == 12 ? item.Position : item.Scale;
      for (size_t i = 0; i < 2; ++i)
        axes[i].Target(command.Vector[i], command.Transition);
      break;
    }
    case 16:
      item.Loop = item.Motion(0, command.Value);
      model.SetMotionLoop(*item.Loop, true);
      model.StartMotion(*item.Loop, 0);
      break;
    case 17:
      item.QueuedLoop = item.Motion(0, command.Value);
      model.SetMotionLoop(*item.QueuedLoop, true);
      break;
    case 21:
      item.Color = command.Color;
      break;
    case 23:
      item.Anger = item.Motion(2, command.Value);
      item.AngerHandle = model.StartMotion(*item.Anger, 1);
      model.SetMotionLoop(*item.Anger, true);
      item.AngerRamp = command.Value != 0;
      if (item.AngerRamp)
        for (auto& tween : item.AngerTweens) tween.Enabled = false;
      break;
    case 24:
      model.StartMotion(item.Motion(3, command.Value), 2);
      item.SweatRamp = command.Value == 1;
      item.SweatTween.Start(100, item.SweatRamp ? 0 : 100, 500, now,
                            item.SweatRamp);
      break;
    case 25:
      if (command.Value == 1) {
        item.Tear = item.Motion(4, 0);
        item.TearHandle = model.StartMotion(*item.Tear, 3);
        item.TearStopping = false;
      } else
        item.TearStopping = true;
      break;
    default:
      throw UnsupportedCommand("Live2D SDK selector not implemented");
  }
}
void SdkLive2dDriver::Remove(const Live2dRecord& record) {
  if (record.ResourceId) Get(*record.ResourceId).Attached = false;
}
void SdkLive2dDriver::Reset() { items_.clear(); }
void SdkLive2dDriver::Restore(const Live2dState& state, uint64_t now) {
  if (!state.Known)
    throw UnsupportedCommand("Live2D restore requires known records");
  for (const auto& parameter : state.Parameters)
    if (!std::isfinite(parameter.Value))
      throw FormatError("non-finite restored Live2D parameter");
  SdkLive2dDriver prepared(resources_, environment_);
  for (const auto& record : state.Records) {
    if (!record.ResourceId || !record.Position || !record.Scale ||
        !record.Enabled || !record.Color || !record.AnimationIndex ||
        !record.Pose)
      throw UnsupportedCommand("Live2D restore record fields are incomplete");
    if (prepared.ResourceReady(*record.ResourceId))
      throw FormatError("duplicate Live2D restore resource");
    prepared.RequestResource(*record.ResourceId, record.LogicalId, now);
    prepared.Attach(record, record.LogicalId, now);
    const auto apply = [&](uint32_t selector, int32_t value,
                           const std::array<float, 2>& vector,
                           const std::array<uint8_t, 4>& color) {
      Live2dCommand command;
      command.Selector = selector;
      command.Target = record.LogicalId;
      command.Value = value;
      command.Vector = vector;
      command.Color = color;
      prepared.Apply(record, command, now);
    };
    apply(3, *record.Enabled, {}, {});
    apply(13, 0, *record.Scale, {});
    apply(12, 0, *record.Position, {});
    apply(21, 0, {}, *record.Color);
    apply(16, *record.AnimationIndex, {}, {});
    apply(6, *record.Pose, {}, {});
  }
  items_.swap(prepared.items_);
}
void SdkLive2dDriver::Frame(const Live2dState& state, uint64_t now) {
  if (!state.Known)
    throw UnsupportedCommand("Live2D frame requires known model records");
  sdk_.SetTime(now);
  for (const auto& record : state.Records) {
    if (!record.ResourceId) continue;
    auto& item = Get(record);
    item.Animate(now, environment_);
    item.Model->ApplyPose();
    if (const auto value = Live2dParameterValue(state, record.LogicalId))
      item.Model->SetParameter("PARAM_YAMI", *value);
    for (auto& axis : item.Position) axis.Tick();
    for (auto& axis : item.Scale) axis.Tick();
    item.Model->Update();
  }
}
void SdkLive2dDriver::SetMouth(int32_t resource, bool enabled, uint64_t now) {
  auto& item = Get(resource);
  item.MouthEnvelopeBound=false;
  if (enabled && !item.MouthActive) item.MouthEpoch = now;
  item.MouthActive = enabled;
}
void SdkLive2dDriver::BeginMouthEnvelope(int32_t resource,const ProfileMouthAnimation& envelope,uint64_t now) {
  auto& item=Get(resource);item.Envelope.Begin(envelope,now);item.MouthEnvelopeBound=true;
}
void SdkLive2dDriver::EndMouthEnvelope(int32_t resource) {
  auto& item=Get(resource);item.Envelope.Stop();item.MouthEnvelopeBound=true;
}
OpenGL::Cubism2Model& SdkLive2dDriver::Model(int32_t resource) {
  return *Get(resource).Model;
}
const Live2dModelAssets& SdkLive2dDriver::Assets(int32_t resource) const {
  return Get(resource).Assets;
}
Live2dDriverObservation SdkLive2dDriver::Observe(int32_t resource) const {
  const auto& item = Get(resource);
  Live2dDriverObservation result;
  for (size_t i = 0; i < 2; ++i) {
    result.Position[i] = item.Position[i].Current;
    result.Scale[i] = item.Scale[i].Current;
  }
  for (size_t i = 0; i < 4; ++i) {
    result.EyeCurrent[i] = item.Eyes[i].Current;
    result.EyeTarget[i] = item.Eyes[i].To;
  }
  result.Anchor = item.Anchor;
  result.Color = item.Color;
  result.Enabled = item.Enabled;
  result.OneShotPending = item.PendingOneShot.has_value();
  result.OneShotActive = item.OneShotActive;
  result.Loop = item.Loop;
  result.QueuedLoop = item.QueuedLoop;
  result.Expression = item.Expression;
  result.SweatRamp = item.SweatRamp;
  result.TearSelected = item.Tear.has_value();
  result.TearStopping = item.TearStopping;
  result.MouthActive = item.MouthActive;
  result.Mouth = item.Mouth;
  return result;
}
std::array<float, 16> SdkLive2dDriver::Matrix(int32_t resource, float width,
                                              float height) const {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
      height <= 0)
    throw FormatError("invalid Live2D viewport");
  const auto& item = Get(resource);
  const auto x = item.Position[0].Current, y = item.Position[1].Current;
  const auto sx = item.Scale[0].Current, sy = item.Scale[1].Current;
  return {2 * sx / width,
          0,
          0,
          0,
          0,
          -2 * sy / height,
          0,
          0,
          0,
          0,
          0.5f,
          0,
          2 * (x - sx * float(item.Anchor[0])) / width - 1,
          1 - 2 * (y - sy * float(item.Anchor[1])) / height,
          0.5f,
          1};
}
void SdkLive2dDriver::Draw(const Live2dState& state, float width,
                           float height) {
  if (!state.Known)
    throw UnsupportedCommand("Live2D draw requires known model records");
  for (const auto& record : state.Records) {
    if (!record.ResourceId) continue;
    auto& item = Get(record);
    if (!item.Enabled) continue;
    item.Model->SetMatrix(Matrix(*record.ResourceId, width, height));
    // Native records retain BGRA bytes; the OpenGL SDK adapter takes RGBA.
    item.Model->SetColor(float(item.Color[2]) / 255, float(item.Color[1]) / 255,
                         float(item.Color[0]) / 255,
                         float(item.Color[3]) / 255);
    item.Model->Draw();
  }
}
}  // namespace Impacto::Saekano
