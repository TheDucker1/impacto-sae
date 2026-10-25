#include "profile.h"
#include "commands.h"
#include "seekmap.h"
#include "profilearrow.h"

#include <bit>
#include <cmath>
#include <algorithm>
#include <limits>

namespace Impacto::Saekano {
namespace {
// Native narrow tokenizer801815d8. Only CRLF ends a row; bare CR/LF delimit
// fields. The profile caller resets row-end only after rejecting a non-ON
// token.
class Tokens {
 public:
  explicit Tokens(std::span<const uint8_t> text) : text_(text) {}
  bool Done() const { return position_ == text_.size(); }
  void BeginRow() { ended_ = Done(); }
  std::string Next() {
    std::string result;
    if (ended_) return result;
    while (!Done()) {
      const auto c = text_[position_++];
      if (c == '"') {
        while (!Done()) {
          const auto q = text_[position_++];
          if (q != '"')
            result.push_back(char(q));
          else if (!Done() && text_[position_] == '"') {
            ++position_;
            result.push_back('"');
          } else
            break;
        }
      } else if (c == ',' || c == '\r' || c == '\n') {
        if (c == '\r' && !Done() && text_[position_] == '\n') {
          ++position_;
          ended_ = true;
        }
        ended_ = ended_ || Done();
        return result;
      } else
        result.push_back(char(c));
    }
    ended_ = true;
    return result;
  }
  void FinishRow() {
    while (!ended_) Next();
  }

 private:
  std::span<const uint8_t> text_;
  size_t position_ = 0;
  bool ended_ = false;
};
int32_t Number(std::string_view text) {
  // Imported _Stoul(base10), unsigned32 then native signed32 field store.
  size_t i = 0;
  while (i < text.size() &&
         (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
          text[i] == '\n' || text[i] == '\v' || text[i] == '\f'))
    ++i;
  bool negative = i < text.size() && text[i] == '-';
  if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
  const auto first = i;
  uint32_t value = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    const auto digit = uint32_t(text[i++] - '0');
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10)
      throw FormatError("profile numeric value exceeds unsigned32 range");
    value = value * 10 + digit;
  }
  if (i == first)
    throw FormatError("profile numeric token contains no decimal digits");
  return std::bit_cast<int32_t>(negative ? 0u - value : value);
}
std::string DualText(std::string text) {
  // The native dual-string constructor receives a C string, not its CSV length.
  if (const auto end = text.find('\0'); end != std::string::npos)
    text.resize(end);
  return text;
}
void CheckOwner(const ProfileOwnerState& owner) {
  if (!owner.Known)
    throw UnsupportedCommand("initial owner profile stats are missing");
}
size_t CharacterStatIndex(int32_t id) {
  if (id < 0 || id >= 4)
    throw FormatError("profile character ID outside owner stat array");
  return size_t(id) * 3;
}
void AddStat(int32_t& value, int32_t change) {
  value = std::bit_cast<int32_t>(uint32_t(value) + uint32_t(change));
  if (value > 100) value = 100;
}
class EnvironmentCursor {
 public:
  explicit EnvironmentCursor(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  void Skip(size_t count) {
    if (count > bytes_.size() - position_)
      throw FormatError("truncated profile environment prefix");
    position_ += count;
  }
  uint32_t Word() {
    const auto at = position_;
    Skip(4);
    return uint32_t(bytes_[at]) | uint32_t(bytes_[at + 1]) << 8 |
           uint32_t(bytes_[at + 2]) << 16 | uint32_t(bytes_[at + 3]) << 24;
  }
  int32_t Signed() { return std::bit_cast<int32_t>(Word()); }
  uint32_t Count(size_t minimum) {
    const auto count = Word();
    if (count > 1000000 || count > (bytes_.size() - position_) / minimum)
      throw FormatError(
          "profile environment collection exceeds available input/budget");
    return count;
  }
  void Counted(size_t stride) {
    const auto count = Count(stride);
    Skip(size_t(count) * stride);
  }
  std::u16string String() {
    const auto count = Count(2);
    const auto at = position_;
    Skip(size_t(count) * 2);
    std::u16string result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
      result.push_back(char16_t(uint16_t(bytes_[at + i * 2]) |
                                uint16_t(bytes_[at + i * 2 + 1]) << 8));
    return result;
  }
  UiSpriteDefinition Sprite() {
    const auto at = position_;
    Skip(36);
    PayloadReader reader(bytes_.subspan(at, 36));
    return UiSpriteDefinition::Read(reader);
  }
  std::array<int32_t, 2> Vector() { return {Signed(), Signed()}; }
  size_t Position() const { return position_; }

 private:
  std::span<const uint8_t> bytes_;
  size_t position_ = 0;
};
}  // namespace
ProfileEnvironment ProfileEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() > 32 * 1024 * 1024)
    throw FormatError("profile environment exceeds input budget");
  EnvironmentCursor cursor(bytes);
  ProfileEnvironment result;
  cursor.Skip(3 + 3 * 4);
  result.IntroFade = cursor.Signed();
  // Native reader801c0cdc consumes nine words (36 bytes), distinct from the
  // older34-byte sprite descriptor. Rect and vector2 readers consume16 and8.
  cursor.Skip(3 * 36);
  result.WavePeriod = cursor.Signed();
  result.Chrome.Frame = cursor.Sprite();  // e734
  result.Hud.BeforePortrait = cursor.Sprite();  // e758
  result.Chrome.Header = cursor.Sprite();  // e77c
  result.Chrome.Stretch = cursor.Sprite();  // e7a0
  result.Chrome.StretchSize = cursor.Vector();  // e7c4
  result.Chrome.EarlyOverlay = cursor.Sprite();  // e7cc
  result.Chrome.LateOverlay = cursor.Sprite();  // e7f0
  cursor.Skip(4);
  for (auto& sprite : result.Hud.AfterPortrait) sprite = cursor.Sprite();
  result.Hud.StatBar = cursor.Sprite();  // e8a8
  const auto statColors = cursor.Count(4);
  for (uint32_t i = 0; i < statColors; ++i) {
    const auto color = cursor.Word();
    result.Hud.StatColors.push_back({uint8_t(color >> 16), uint8_t(color >> 8),
                                     uint8_t(color), uint8_t(color >> 24)});
  }
  result.Portrait.Sprite = cursor.Sprite();  // e8dc
  for (auto& value : result.Portrait.Area) value = cursor.Signed();  // e900
  result.Hud.CharacterName = cursor.Sprite();  // e910
  for (auto& sprite : result.Navigation.Sprites) sprite = cursor.Sprite();
  result.Navigation.Travel = std::bit_cast<float>(cursor.Word());  // e97c
  result.Navigation.Duration = cursor.Signed();  // e980
  result.ArrowTravel = std::bit_cast<float>(cursor.Word());  // e984
  result.ArrowDuration = cursor.Signed();  // e988
  for (auto& sprite : result.Arrows.Upper) sprite = cursor.Sprite();
  for (auto& sprite : result.Arrows.Lower) sprite = cursor.Sprite();
  cursor.Skip(2 * 16);
  result.ItemFrames.Sprite = cursor.Sprite();  // ea3c
  const auto itemPositions = cursor.Count(8);
  for (uint32_t i = 0; i < itemPositions; ++i)
    result.ItemFrames.Positions.push_back(cursor.Vector());  // ea60
  result.ItemContents.Selector = cursor.Sprite();  // ea70
  const auto categories = cursor.Count(36);
  for (uint32_t i = 0; i < categories; ++i)
    result.ItemContents.Categories.push_back(cursor.Sprite());  // ea94
  result.ItemContents.LabelOffset = cursor.Vector();  // eaa4
  result.ItemContents.LabelScale = std::bit_cast<float>(cursor.Word());  // eaac
  cursor.Skip(3 * 4);
  result.Carousel = cursor.Signed();
  const auto descriptionPositions = cursor.Count(8);
  for (uint32_t i = 0; i < descriptionPositions; ++i)
    result.DescriptionPositions.push_back(cursor.Vector());  // eac0
  result.DescriptionScale = std::bit_cast<float>(cursor.Word());  // ead0
  result.DescriptionColumns = cursor.Word();  // ead4
  result.Chrome.Footer = cursor.Sprite();  // ead8
  cursor.Skip(36);
  result.ResultProgress = cursor.Signed();
  cursor.Skip(8);
  cursor.String();
  const auto labels = cursor.Count(8);  // UTF16 string plus packed color.
  for (uint32_t i = 0; i < labels; ++i) {
    cursor.String();
    cursor.Skip(4);
  }
  cursor.String();
  cursor.Skip(4);
  result.ResultReflection = cursor.Sprite();  // ebb0
  for (size_t group = 0; group < 3; ++group) {
    const auto strings = cursor.Count(4);
    for (uint32_t i = 0; i < strings; ++i)
      result.ResultLabels[group].push_back(cursor.String());
  }
  result.ResultFormat = cursor.String();
  result.Chrome.Marker = cursor.Sprite();  // ec3c
  const auto positions = cursor.Count(8);
  for (uint32_t i = 0; i < positions; ++i)
    result.Chrome.MarkerPositions.push_back(cursor.Vector());
  result.SecondaryProgress = cursor.Signed();
  result.Chrome.MarkerStep = cursor.Vector();  // ec74
  result.StatChange = cursor.Signed();
  result.PrefixBytes = cursor.Position();
  for (const auto duration :
       {result.IntroFade, result.WavePeriod, result.Carousel, result.ResultProgress,
        result.SecondaryProgress, result.StatChange})
    if (duration < 0) throw FormatError("negative profile transition duration");
  return result;
}
ProfileResponseVisualEnvironment ProfileResponseVisualEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  const auto profile = ProfileEnvironment::Decode(bytes);
  EnvironmentCursor cursor(bytes);
  cursor.Skip(profile.PrefixBytes);
  ProfileResponseVisualEnvironment result;
  result.Window = cursor.Sprite();                         // ec80
  result.TextPosition = cursor.Vector();                   // eca4
  result.TextWidth = cursor.Signed();                      // ecac
  result.TextScale = std::bit_cast<float>(cursor.Word());  // ecb0
  cursor.String();  // Confirmation prompt, not response text.
  cursor.Skip(2 * 16);
  for (auto& sprite : result.Cursors) sprite = cursor.Sprite();  // ed0c/ed30
  result.PrefixBytes = cursor.Position();
  if (result.TextWidth <= 0 || !std::isfinite(result.TextScale) ||
      result.TextScale <= 0)
    throw FormatError("invalid profile response text layout");
  return result;
}
ProfileCursorEnvironment ProfileCursorEnvironment::Decode(
    std::span<const uint8_t> bytes) {
  const auto profile = ProfileEnvironment::Decode(bytes);
  EnvironmentCursor cursor(bytes);
  cursor.Skip(profile.PrefixBytes + 13 * 4);
  cursor.String();
  cursor.Skip(2 * 16 + 2 * 36);
  ProfileCursorEnvironment result;
  result.Duration = cursor.Signed();
  result.Travel = std::bit_cast<float>(cursor.Word());
  result.PrefixBytes = cursor.Position();
  if (result.Duration < 0 || !std::isfinite(result.Travel))
    throw FormatError("invalid profile cursor motion configuration");
  return result;
}
ProfileInitialStats ProfileInitialStats::Decode(
    std::span<const uint8_t> bytes) {
  const auto environment = ProfileEnvironment::Decode(bytes);
  EnvironmentCursor cursor(bytes);
  cursor.Skip(environment.PrefixBytes + 13 * 4);
  cursor.String();  // Authored confirmation prompt; length relocates the tail.
  cursor.Skip(2 * 16 + 2 * 36 + 2 * 4);
  const auto rows = cursor.Count(4);
  if (rows > 4)
    throw FormatError("profile initial stats exceed four characters");
  ProfileInitialStats result;
  for (uint32_t i = 0; i < rows; ++i) {
    const auto count = cursor.Count(4);
    if (count > 3)
      throw FormatError("profile initial stats exceed three fields");
    for (uint32_t j = 0; j < count; ++j)
      result.Values[i * 3 + j] = cursor.Signed();
  }
  result.PrefixBytes = cursor.Position();
  return result;
}
std::u16string BuildProfileResultText(const ProfileOwnerState& owner,
                                      const ProfileEnvironment& environment,
                                      int32_t characterId, size_t ordinal,
                                      const ProfileEvent& event) {
  if (!owner.ResultCategories)
    throw UnsupportedCommand("initial profile result categories are missing");
  if (owner.ResultCategories->size() != 4)
    throw FormatError("profile result owner table must contain four rows");
  if (ordinal >= 4 || event.Values[0] < 0 || event.Values[0] >= 30)
    throw FormatError("profile result event outside owner table");
  const auto category =
      (*owner.ResultCategories)[ordinal][size_t(event.Values[0])];
  if (characterId < 0 ||
      size_t(characterId) >= environment.ResultLabels[0].size() ||
      category < 0 || size_t(category) >= environment.ResultLabels[1].size() ||
      size_t(category) >= environment.ResultLabels[2].size())
    throw FormatError("profile result label outside environment table");
  const auto cString = [](const std::u16string& text) {
    return std::u16string_view(text).substr(0, text.find(u'\0'));
  };
  const std::array<std::u16string_view, 3> arguments{
      cString(environment.ResultLabels[0][size_t(characterId)]),
      cString(environment.ResultLabels[2][size_t(category)]),
      cString(environment.ResultLabels[1][size_t(category)])};
  const auto format = cString(environment.ResultFormat);
  std::u16string result;
  size_t argument = 0;
  for (size_t i = 0; i < format.size();) {
    if (format[i] != u'%')
      result.push_back(format[i++]);
    else if (format.substr(i, 2) == u"%%") {
      result.push_back(u'%');
      i += 2;
    } else if (format.substr(i, 3) == u"%ls" && argument < arguments.size()) {
      result.append(arguments[argument++]);
      i += 3;
    } else
      throw UnsupportedCommand("unsupported profile result format directive");
    if (result.size() >= 128)
      throw FormatError("profile result text exceeds native128-unit buffer");
  }
  if (argument != arguments.size())
    throw FormatError("profile result format does not consume three labels");
  return result;
}
ProfileScene DecodeProfileScene(std::span<const uint8_t> bytes) {
  if (bytes.size() > 32 * 1024 * 1024)
    throw FormatError("profile scene exceeds input budget");
  Tokens tokens(bytes);
  ProfileScene result;
  while (!tokens.Done()) {
    if (tokens.Next() != "ON") {
      tokens.FinishRow();
      tokens.BeginRow();
      continue;
    }
    const auto kind = tokens.Next();
    if (kind == "CHARA") {
      if (result.size() >= 1000000)
        throw FormatError("profile character count exceeds host budget");
      result.push_back({Number(tokens.Next()), {}});
    } else if (kind == "DATA") {
      if (result.empty()) throw FormatError("profile DATA precedes CHARA");
      auto& events = result.back().Events;
      if (events.size() == 10)
        throw FormatError("profile rows exceed native ten-entry capacity");
      ProfileEvent event;
      for (auto& value : event.Values) value = Number(tokens.Next());
      event.ScriptName = DualText(tokens.Next());
      for (auto& value : event.Presentation) value = Number(tokens.Next());
      event.Response = DualText(tokens.Next());
      event.Tail = Number(tokens.Next());
      events.push_back(std::move(event));
    }
  }
  return result;
}
float ProfileVoiceGain(uint8_t option, float channelCoefficient) {
  if (!(channelCoefficient >= 0) || channelCoefficient > 1)
    throw FormatError("profile voice channel coefficient outside known range");
  return (float(uint32_t(option) * 10) * channelCoefficient) / 100.0f;
}
ProfileMouthAnimation DecodeProfileMouth(std::span<const uint8_t> bytes) {
  if (bytes.size() > 1024 * 1024)
    throw FormatError("profile mouth data exceeds budget");
  const auto comma = std::find(bytes.begin(), bytes.end(), uint8_t(','));
  if (comma == bytes.end())
    throw FormatError("profile mouth data has no duration separator");
  const size_t length = size_t(comma - bytes.begin());
  size_t i = 0;
  while (i < length &&
         (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r' ||
          bytes[i] == '\n' || bytes[i] == '\v' || bytes[i] == '\f'))
    ++i;
  bool negative = i < length && bytes[i] == '-';
  if (i < length && (bytes[i] == '+' || bytes[i] == '-')) ++i;
  uint32_t duration = 0;
  for (; i < length && bytes[i] >= '0' && bytes[i] <= '9'; ++i) {
    const auto digit = uint32_t(bytes[i] - '0');
    if (duration > (uint32_t(INT32_MAX) - digit) / 10)
      throw FormatError("profile mouth duration exceeds signed32 range");
    duration = duration * 10 + digit;
  }
  if (negative && duration)
    throw FormatError("negative profile mouth duration");
  ProfileMouthAnimation result;
  result.Duration = int32_t(duration);
  result.Samples.reserve(bytes.size() - length - 1);
  // Native5204e..52238 transforms every remaining byte, without whitespace
  // trimming or C-string termination. Preserve its unsigned byte subtraction.
  for (size_t at = length + 1; at < bytes.size(); ++at)
    result.Samples.push_back(uint8_t(int(bytes[at]) - '0'));
  return result;
}
size_t InitialProfileOrdinal(const ProfileOwnerState& owner,
                             const ProfileScene& scene) {
  CheckOwner(owner);
  if (scene.empty() || scene.size() > 4)
    throw FormatError("profile character count outside mode capacity");
  if (owner.CachedOrdinal < 0)
    throw FormatError("negative cached profile ordinal");
  return size_t(owner.CachedOrdinal) < scene.size()
             ? size_t(owner.CachedOrdinal)
             : 0;
}
void StepProfileSelector(uint32_t& selector, bool forward) {
  if (selector >= 10)
    throw FormatError("profile selector outside native cycle");
  selector = (selector + (forward ? 1u : 9u)) % 10;
}
void BeginProfileSelection(ProfileSelectionState& selection,
                           const ProfileOwnerState& owner,
                           const ProfileScene& scene) {
  ProfileSelectionState next;
  next.Started = true;
  next.Ordinal = InitialProfileOrdinal(owner, scene);
  next.CharacterCount = scene.size();
  next.ReturnPhase = scene.size() == 1 ? 6 : 5;
  selection = next;
}
bool UpdateProfileSelection(ProfileSelectionState& selection,
                            const ProfileEnvironment& environment,
                            const ProfileSelectionInput& input, uint64_t now) {
  if (!selection.Started)
    throw UnsupportedCommand("profile selection resources are not initialized");
  selection.UiSounds.clear();
  if (selection.PendingPhase) {
    selection.Phase = *selection.PendingPhase;
    selection.PendingPhase.reset();
  }
  if (selection.Phase < 3 || selection.Phase > 10)
    throw FormatError("invalid profile selection phase");
  if (selection.CharacterCount == 0 || selection.CharacterCount > 4 ||
      selection.Ordinal >= selection.CharacterCount)
    throw FormatError("profile selection outside loaded characters");
  // The native automation's character:<ordinal> action changes only108.
  if (input.Ordinal) {
    if (*input.Ordinal >= selection.CharacterCount)
      throw FormatError("profile input ordinal outside loaded characters");
    selection.Ordinal = *input.Ordinal;
  }
  if (selection.Phase == 3) {
    selection.Fade.Start(255, 0, environment.IntroFade, now);
    selection.Phase = 4;
  }
  if (selection.Phase == 4) {
    selection.Fade.Tick(now);
    if (selection.Fade.Finished())
      selection.PendingPhase = selection.ReturnPhase;
  }
  if (selection.Phase == 5) {
    if (input.CharacterDirection) {
      selection.UiSounds.push_back(2);
      const auto count = selection.CharacterCount;
      selection.Ordinal =
          (selection.Ordinal + (*input.CharacterDirection ? 1 : count - 1)) %
          count;
    } else if (input.Accept) {
      selection.UiSounds.push_back(3);
      selection.PendingPhase = 6;
    }
  }
  if (selection.Phase == 6) {
    selection.FrameOffsets = {};
    if (input.EventDirection) {
      if (environment.ItemFrames.Positions.size() < 8)
        throw UnsupportedCommand(
            "profile carousel requires eight authored positions");
      selection.UiSounds.push_back(2);
      for (size_t i = 0; i < 8; ++i) {
        std::array<int32_t, 2> delta{};
        const bool hasNeighbor = *input.EventDirection ? i > 0 : i < 7;
        if (hasNeighbor) {
          const auto& from = environment.ItemFrames.Positions[i];
          const auto& to =
              environment.ItemFrames
                  .Positions[*input.EventDirection ? i - 1 : i + 1];
          for (size_t axis = 0; axis < 2; ++axis)
            delta[axis] = std::bit_cast<int32_t>(uint32_t(to[axis]) -
                                                 uint32_t(from[axis]));
        }
        selection.CarouselX[i].Start(0, delta[0], environment.Carousel, now);
        selection.CarouselY[i].Start(0, delta[1], environment.Carousel, now);
      }
      selection.PendingPhase = 7;
      selection.ArrowActive[*input.EventDirection ? 0 : 1] = true;
    } else if (input.Accept) {
      selection.UiSounds.push_back(3);
      selection.PendingPhase = 8;
    } else if (input.Cancel && selection.CharacterCount != 1) {
      selection.UiSounds.push_back(4);
      selection.PendingPhase = 5;
    }
  }
  if (selection.Phase == 7) {
    bool finished = true;
    for (size_t i = 0; i < 8; ++i) {
      selection.CarouselX[i].Tick(now);
      selection.CarouselY[i].Tick(now);
      finished = finished && selection.CarouselX[i].Finished();
      selection.FrameOffsets[i] = {selection.CarouselX[i].Current,
                                   selection.CarouselY[i].Current};
    }
    if (finished) {
      const auto direction = selection.CarouselY[3].To;
      if (direction != 0)
        StepProfileSelector(selection.Selectors[selection.Ordinal],
                            direction < 0);
      for (size_t i = 0; i < 8; ++i) {
        selection.CarouselX[i].Start(0, 0, 0, now);
        selection.CarouselY[i].Start(0, 0, 0, now);
      }
      selection.PendingPhase = 6;
      selection.ArrowActive = {};
    }
  }
  if (selection.Phase == 8) {
    selection.ConfirmationOpen = true;
    selection.Phase = 9;
  }
  if (selection.Phase == 9 && input.Confirmation) {
    selection.ConfirmationOpen = false;
    selection.PendingPhase = *input.Confirmation ? 10 : 6;
  }
  return selection.Phase == 10;
}
const ProfileEvent& SelectedProfileEvent(const ProfileScene& scene,
                                         size_t ordinal, uint32_t selector) {
  if (ordinal >= scene.size() || selector >= 10)
    throw FormatError("profile selection outside scene");
  const size_t index = (selector + 3) % 10;
  if (index >= scene[ordinal].Events.size())
    throw UnsupportedCommand(
        "selected profile event has unwritten native fields");
  return scene[ordinal].Events[index];
}
void StartProfileStatChanges(const ProfileOwnerState& owner,
                             ProfileStatAnimation& animation,
                             int32_t characterId, const ProfileEvent& event,
                             int32_t duration, uint64_t now) {
  CheckOwner(owner);
  CharacterStatIndex(characterId);
  if (duration < 0)
    throw FormatError("negative profile stat transition duration");
  ProfileStatAnimation next;
  next.Started = true;
  next.CharacterId = characterId;
  for (size_t i = 0; i < 3; ++i)
    next.Changes[i].Start(0, event.Values[i + 2], duration, now);
  animation = next;
}
void TickProfileStatChanges(ProfileOwnerState& owner,
                            ProfileStatAnimation& animation, uint64_t now) {
  CheckOwner(owner);
  if (!animation.Started)
    throw UnsupportedCommand("profile stat Start was suppressed");
  const auto base = CharacterStatIndex(animation.CharacterId);
  for (size_t i = 0; i < 3; ++i) {
    auto& change = animation.Changes[i];
    change.Tick(now);
    if (change.Finished()) {
      AddStat(owner.Stats[base + i], change.Current);
      change.Start(0, 0, 0, now);
    }
  }
}
void FinishProfileStatChanges(ProfileOwnerState& owner,
                              ProfileStatAnimation& animation, uint64_t now) {
  CheckOwner(owner);
  if (!animation.Started)
    throw UnsupportedCommand("profile stat Start was suppressed");
  const auto base = CharacterStatIndex(animation.CharacterId);
  for (size_t i = 0; i < 3; ++i) {
    AddStat(owner.Stats[base + i], animation.Changes[i].To);
    animation.Changes[i].Start(0, 0, 0, now);
  }
}
void RequestProfileCategoryTrophy(SemanticState& state,
                                  const ProfileEvent& event) {
  const auto category = event.Values[1];
  if (category >= 0 && category <= 2)
    state.PendingEvents.push_back({GameEventKind::Trophy, 34 - category});
}
void RequestProfileStatTrophies(SemanticState& state, int32_t characterId) {
  CheckOwner(state.ProfileOwner);
  const auto base = CharacterStatIndex(characterId);
  for (size_t i = 0; i < 3; ++i)
    if (state.ProfileOwner.Stats[base + i] == 100)
      state.PendingEvents.push_back({GameEventKind::Trophy, int32_t(26 + i)});
}
void PublishProfileStats(SemanticState& state, int32_t ordinal) {
  CheckOwner(state.ProfileOwner);
  if (ordinal < 0 || ordinal >= 4)
    throw FormatError("profile ordinal outside mode capacity");
  for (size_t i = 0; i < 12; ++i)
    state.Bank0[30 + i] =
        std::bit_cast<int16_t>(uint16_t(state.ProfileOwner.Stats[i]));
  state.ProfileOwner.CachedOrdinal = ordinal;
}
void BeginProfileResult(ProfileResultState& result, const ProfileScene& scene,
                        size_t ordinal, uint32_t selector) {
  if (scene.empty() || scene.size() > 4 || ordinal >= 4)
    throw FormatError("profile result outside mode character capacity");
  ProfileResultState next;
  next.Event = SelectedProfileEvent(scene, ordinal, selector);
  next.CharacterId = scene[ordinal].Id;
  CharacterStatIndex(next.CharacterId);
  next.Ordinal = ordinal;
  next.Started = true;
  result = std::move(next);
}
bool UpdateProfileResult(SemanticState& state, ProfileResultState& result,
                         const ProfileEnvironment& environment,
                         const ModeResultEnvironment& shared,
                         const ProfileResultInput& input, uint64_t now) {
  if (!result.Started)
    throw UnsupportedCommand("profile result has no confirmed selection");
  CheckOwner(state.ProfileOwner);
  result.UiSounds.clear();
  result.RevealRequested = false;
  result.CursorTickRequested = false;
  if (result.PendingPhase) {
    result.Phase = *result.PendingPhase;
    result.PendingPhase.reset();
  }
  const auto setup = [now](ScriptLinearTween& tween, int32_t from, int32_t to,
                           int32_t duration) {
    tween.Start(from, to, duration, now);
  };
  const auto activate = [now](ScriptLinearTween& tween) {
    tween.StartUnsigned(tween.From, tween.To, tween.Duration, now);
  };
  if (result.Phase == 10) {
    result.Primary.Start(0, 100, shared.Progress, now);
    setup(result.HorizontalOffset, shared.HorizontalStart, 0,
          shared.HorizontalSlide);
    setup(result.HorizontalAlpha, 0, 255, shared.HorizontalOpacity);
    setup(result.VerticalOffset, shared.VerticalStart, 0, shared.VerticalSlide);
    setup(result.VerticalAlpha, 0, 255, shared.VerticalOpacity);
    setup(result.EntryCompletion, 0, 100, shared.SecondaryProgress);
    setup(result.TextAlpha, 0, 255, shared.TextOpacity);
    result.Visible = true;
    result.Phase = 11;
  }
  if (result.Phase == 11) {
    result.Primary.Tick(now);
    if (result.Primary.Finished()) {
      if (!result.SlidesActive) {
        activate(result.HorizontalOffset);
        activate(result.HorizontalAlpha);
        activate(result.VerticalOffset);
        activate(result.VerticalAlpha);
        result.SlidesActive = true;
      }
      result.HorizontalOffset.Tick(now);
      result.HorizontalAlpha.Tick(now);
      result.VerticalOffset.Tick(now);
      result.VerticalAlpha.Tick(now);
      if (result.HorizontalAlpha.Finished() &&
          result.VerticalAlpha.Finished()) {
        if (!result.CompletionActive) {
          activate(result.EntryCompletion);
          result.CompletionActive = true;
        }
        result.EntryCompletion.Tick(now);
        if (result.EntryCompletion.Finished()) {
          result.Text = BuildProfileResultText(state.ProfileOwner, environment,
                                               result.CharacterId,
                                               result.Ordinal, result.Event);
          result.TextStarted = true;
          result.ResultProgress.Start(0, 100, environment.ResultProgress, now);
          result.SecondaryProgress.Start(0, 100, environment.SecondaryProgress,
                                         now);
          StartProfileStatChanges(state.ProfileOwner, result.Stats,
                                  result.CharacterId, result.Event,
                                  environment.StatChange, now);
          result.Phase = 12;
        }
      }
    }
  }
  if (result.Phase == 12) {
    result.ResultProgress.Tick(now);
    result.SecondaryProgress.Tick(now);
    TickProfileStatChanges(state.ProfileOwner, result.Stats, now);
    const bool revealed = input.Text ? input.Text->Update(result.Text, now)
                                    : input.TextRevealed;
    if (!revealed) {
      result.RevealRequested = input.Accept;
      if (result.RevealRequested && input.Text) input.Text->Reveal(now);
    } else {
      if (!result.TextAlphaActive) {
        const auto category = result.Event.Values[1];
        if (category >= 0 && category <= 2)
          result.UiSounds.push_back(
              std::array<int32_t, 3>{13, 12, 15}[size_t(category)]);
        activate(result.TextAlpha);
        result.TextAlphaActive = true;
      }
      result.TextAlpha.Tick(now);
      result.CursorTickRequested = result.TextAlpha.Finished();
      if (result.TextAlpha.Finished() && input.Accept) {
        result.UiSounds.push_back(3);
        RequestProfileCategoryTrophy(state, result.Event);
        result.Phase = 13;
      }
    }
  }
  if (result.Phase == 13) {
    FinishProfileStatChanges(state.ProfileOwner, result.Stats, now);
    result.Phase = 14;
  }
  if (result.Phase == 14) {
    RequestProfileStatTrophies(state, result.CharacterId);
    result.ResponseMode = true;
    result.Phase = 18;
  }
  if (result.Phase == 18) {
    result.Fade.Start(0, 255, 1500, now);
    result.Phase = 19;
  }
  if (result.Phase == 19) {
    result.Fade.Tick(now);
    if (result.Fade.Finished()) {
      result.Visible = false;
      result.PendingPhase = 15;
    }
  }
  if (result.Phase != 11 && result.Phase != 12 && result.Phase != 15 &&
      result.Phase != 19)
    throw FormatError("invalid profile result phase");
  return result.Phase == 15;
}
void BeginProfileResponse(const SemanticState& state,
                          ProfileResponseState& response,
                          const ProfileResultState& result) {
  CheckOwner(state.ProfileOwner);
  if (!result.Started || result.Phase != 15 || result.PendingPhase ||
      !result.ResponseMode || result.Visible)
    throw UnsupportedCommand(
        "profile result has not reached response resource entry");
  if (!state.LastBackgroundId)
    throw UnsupportedCommand("initial profile response background is missing");
  if (*state.LastBackgroundId < -1 || result.Ordinal >= 4 ||
      result.CharacterId < 0 || result.CharacterId >= 4 ||
      result.Event.Presentation[0] < 0 || result.Event.Presentation[1] < 0 ||
      result.Event.Tail < 0)
    throw FormatError("invalid profile response resource selection");
  ProfileResponseState next;
  next.Started = true;
  next.CharacterId = result.CharacterId;
  next.Ordinal = result.Ordinal;
  next.Background = *state.LastBackgroundId;
  next.Event = result.Event;
  next.Fade = result.Fade;
  response = std::move(next);
}
bool UpdateProfileResponse(SemanticState& state, ProfileResponseState& response,
                           const ProfileEnvironment& environment,
                           const ScriptNameIndex& scripts,
                           const ProfileResponseInput& input, uint64_t now) {
  if (!response.Started)
    throw UnsupportedCommand("profile response has not been initialized");
  response.Actions.clear();
  response.CursorTickRequested = false;
  response.UiSounds.clear();
  if (response.Completed) return true;
  if (response.PendingPhase) {
    response.Phase = *response.PendingPhase;
    response.PendingPhase.reset();
  }
  // Keep native block order: fade3/4 precedes response loading15/16.
  if (response.Phase == 3) {
    response.Fade.Start(255, 0, environment.IntroFade, now);
    response.Phase = 4;
  }
  if (response.Phase == 4) {
    response.Fade.Tick(now);
    if (response.Fade.Finished()) {
      response.Actions.push_back(ProfileResponseAction::BeginTextReveal);
      response.Actions.push_back(ProfileResponseAction::StartVoice);
      response.PendingPhase = 17;
    }
  }
  if (response.Phase == 15) {
    response.Actions.push_back(ProfileResponseAction::BeginResources);
    response.Phase = 16;
  }
  if (response.Phase == 16 && input.ResourcesReady) {
    response.Visible = true;
    response.Actions.push_back(ProfileResponseAction::PreparePresentation);
    response.Actions.push_back(ProfileResponseAction::PrepareVoice);
    response.PendingPhase = 3;
  }
  if (response.Phase == 17) {
    response.Actions.push_back(ProfileResponseAction::UpdatePresentation);
    response.CursorTickRequested = input.TextRevealed;
    if (!input.TextRevealed) {
      if (input.Accept)
        response.Actions.push_back(ProfileResponseAction::RevealText);
    } else if (input.Accept) {
      response.UiSounds.push_back(3);
      response.Phase = 18;
    }
  }
  if (response.Phase == 18) {
    response.Fade.Start(0, 255, 1500, now);
    response.Phase = 19;
  }
  if (response.Phase == 19) {
    response.Fade.Tick(now);
    if (response.Fade.Finished()) response.PendingPhase = 20;
  }
  if (response.Phase == 20) {
    response.Actions.push_back(ProfileResponseAction::StopVoice);
    if (input.VoiceStopped) {
      PublishProfileStats(state, int32_t(response.Ordinal));
      response.Actions.push_back(ProfileResponseAction::StopBgm);
      response.TargetScript = scripts.Resolve(response.Event.ScriptName);
      response.Completed = true;
    }
  }
  if (response.Phase != 4 && response.Phase != 16 && response.Phase != 17 &&
      response.Phase != 19 && response.Phase != 20)
    throw FormatError("invalid profile response phase");
  return response.Completed;
}
}  // namespace Impacto::Saekano
