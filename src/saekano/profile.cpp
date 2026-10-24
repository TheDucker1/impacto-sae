#include "profile.h"
#include "commands.h"

#include <bit>
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
  void String() { Counted(2); }
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
  cursor.Skip(3 * 36 + 4 + 4 * 36 + 8 + 2 * 36 + 4 + 5 * 36);
  cursor.Counted(4);  // Packed colors.
  cursor.Skip(9 * 36 + 3 * 16 + 4 * 4);
  cursor.Counted(8);
  cursor.Skip(36);
  cursor.Counted(36);
  cursor.Skip(8 + 4 * 4);
  result.Carousel = cursor.Signed();
  cursor.Counted(8);
  cursor.Skip(2 * 4 + 2 * 36);
  result.ResultProgress = cursor.Signed();
  cursor.Skip(8);
  cursor.String();
  const auto labels = cursor.Count(8);  // UTF16 string plus packed color.
  for (uint32_t i = 0; i < labels; ++i) {
    cursor.String();
    cursor.Skip(4);
  }
  cursor.String();
  cursor.Skip(4 + 36);
  for (size_t group = 0; group < 3; ++group) {
    const auto strings = cursor.Count(4);
    for (uint32_t i = 0; i < strings; ++i) cursor.String();
  }
  cursor.String();
  cursor.Skip(36);
  cursor.Counted(8);
  result.SecondaryProgress = cursor.Signed();
  cursor.Skip(8);
  result.StatChange = cursor.Signed();
  result.PrefixBytes = cursor.Position();
  for (const auto duration :
       {result.IntroFade, result.Carousel, result.ResultProgress,
        result.SecondaryProgress, result.StatChange})
    if (duration < 0) throw FormatError("negative profile transition duration");
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
}  // namespace Impacto::Saekano
