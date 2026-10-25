#include "profileitems.h"
#include "profile.h"
#include "commands.h"
#include <cmath>

#include <bit>
#include <limits>

namespace Impacto::Saekano {
namespace {
class WideTokens {
 public:
  explicit WideTokens(std::span<const uint8_t> bytes) {
    if (bytes.size() % 2 || bytes.size() > 4 * 1024 * 1024)
      throw FormatError("invalid profile item UTF16 extent");
    text_.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size(); i += 2)
      text_.push_back(
          char16_t(uint16_t(bytes[i]) | uint16_t(bytes[i + 1]) << 8));
  }
  bool More() const { return at_ < text_.size(); }
  bool Ended() const { return ended_; }
  void NextRow() { ended_ = false; }
  std::u16string Next() {
    std::u16string result;
    if (ended_) return result;
    while (More()) {
      const auto ch = text_[at_++];
      if (ch == u'"') {
        while (More()) {
          const auto quoted = text_[at_++];
          if (quoted == u'"') {
            if (!More() || text_[at_] != u'"') break;
            ++at_;
          }
          result.push_back(quoted);
        }
      } else if (ch == u'\t' || ch == u'\r' || ch == u'\n') {
        if (ch == u'\r' && More() && text_[at_] == u'\n') {
          ++at_;
          ended_ = true;
        }
        if (!More()) ended_ = true;
        return result;
      } else {
        result.push_back(ch);
      }
    }
    ended_ = true;
    return result;
  }

 private:
  std::u16string text_;
  size_t at_ = 0;
  bool ended_ = false;
};
int32_t Number(std::u16string_view text) {
  size_t i = 0;
  while (i < text.size() &&
         (text[i] == u' ' || text[i] == u'\t' || text[i] == u'\r' ||
          text[i] == u'\n' || text[i] == u'\v' || text[i] == u'\f'))
    ++i;
  const bool negative = i < text.size() && text[i] == u'-';
  if (i < text.size() && (text[i] == u'-' || text[i] == u'+')) ++i;
  const auto first = i;
  uint32_t value = 0;
  while (i < text.size() && text[i] >= u'0' && text[i] <= u'9') {
    const auto digit = uint32_t(text[i++] - u'0');
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10)
      throw FormatError("profile item number exceeds unsigned32 range");
    value = value * 10 + digit;
  }
  if (i == first)
    throw FormatError("profile item number has no decimal digits");
  return std::bit_cast<int32_t>(negative ? 0u - value : value);
}
}  // namespace
ProfileDescriptionFormat ProfileDescriptionFormat::Decode(
    std::span<const uint8_t> bytes) {
  if (bytes.size() < 3 || bytes.size() > 32 * 1024 * 1024)
    throw FormatError("invalid global description environment extent");
  PayloadReader reader(bytes.subspan(3));
  const auto words = [&](uint32_t count) {
    reader.CheckCount(count, 4);
    for (uint32_t i = 0; i < count; ++i) reader.U32();
  };
  const auto records = [&](uint32_t stride) {
    const auto count = reader.U32();
    reader.CheckCount(count, size_t(stride) * 4);
    words(count * stride);
  };
  for (int i = 0; i < 4; ++i) reader.String();
  words(2);
  for (int i = 0; i < 9; ++i) reader.String();
  words(3 + 27);
  records(6);
  words(8);
  records(6);
  words(4);
  records(1);
  const auto count = reader.U32();
  reader.CheckCount(count, 4);
  for (uint32_t i = 0; i < count; ++i) reader.String();
  reader.String();
  words(2);
  reader.String();
  reader.String();
  ProfileDescriptionFormat result;
  result.Separator = reader.String();
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
std::array<std::u16string, 3> FormatProfileItemDescription(
    std::u16string_view text, std::u16string_view separator, uint32_t columns) {
  text = text.substr(0, text.find(u'\0'));
  separator = separator.substr(0, separator.find(u'\0'));
  if (separator.empty())
    throw FormatError("empty profile description separator");
  if (text.size() > 2 * 1024 * 1024)
    throw FormatError("profile description exceeds input budget");
  std::array<std::u16string, 3> result;
  size_t row = 0, at = 0;
  const auto append = [&](std::u16string_view part) {
    if (row == result.size())
      throw FormatError("profile description exceeds three owner lines");
    result[row++] = part;
  };
  while (true) {
    const auto end = text.find(separator, at);
    auto part = text.substr(at, end == text.npos ? end : end - at);
    if (part.size() > columns) {
      append(part.substr(0, columns));
      part.remove_prefix(columns);
      if (part.size() > columns) {
        append(part.substr(0, columns));
        part.remove_prefix(columns);
      }
    }
    append(part);
    if (end == text.npos) break;
    at = end + separator.size();
  }
  return result;
}
std::vector<ProfileItemDefinition> DecodeProfileItemTable(
    std::span<const uint8_t> bytes) {
  WideTokens tokens(bytes);
  std::vector<ProfileItemDefinition> result;
  while (tokens.More()) {
    if (tokens.Next() == u"ON") {
      if (result.size() == 30)
        throw FormatError("profile item table exceeds30 rows");
      ProfileItemDefinition row;
      row.Id = Number(tokens.Next());
      row.Category = Number(tokens.Next());
      row.Name = tokens.Next();
      row.Description = tokens.Next();
      result.push_back(std::move(row));
    } else {
      while (!tokens.Ended()) tokens.Next();
    }
    if (tokens.Ended()) tokens.NextRow();
  }
  return result;
}
std::vector<ProfileItemCharacterTable> LoadProfileItemCatalog(Bytes firstDat) {
  GpdaArchive root(std::move(firstDat));
  if (root.Entries().size() <= 3 || root.Entries()[3].Type != 0)
    throw FormatError(
        "first.dat profile catalog section missing or compressed");
  GpdaArchive tables(root.Data(3));
  if (tables.Entries().size() > 4)
    throw FormatError("profile item catalog exceeds four characters");
  std::vector<ProfileItemCharacterTable> result;
  for (size_t i = 0; i < tables.Entries().size(); ++i) {
    if (tables.Entries()[i].Type != 0)
      throw FormatError("profile item table is compressed");
    result.push_back(
        {tables.Entries()[i].Name, DecodeProfileItemTable(tables.Data(i))});
  }
  return result;
}
ProfileItemContents BuildProfileItemContents(
    const ProfileEnvironment& environment, int32_t phase,
    const ProfileCharacter& character, uint32_t selector,
    std::span<const ProfileItemDefinition> ownerRows, int32_t baseFontSize,
    std::span<const std::array<int32_t, 2>> contentOffsets) {
  if (character.Events.size() != 10 || selector >= 10)
    throw FormatError(
        "profile item contents require ten events and a valid selector");
  if (environment.ItemFrames.Positions.size() < 8 ||
      (!contentOffsets.empty() && contentOffsets.size() != 8))
    throw FormatError(
        "profile item contents require eight slot positions/offsets");
  if (ownerRows.size() > 30)
    throw FormatError("profile owner item rows exceed capacity");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto& content = environment.ItemContents;
  auto selectorSprite = content.Selector;
  selectorSprite.X =
      add(selectorSprite.X, environment.ItemFrames.Positions[3][0]);
  selectorSprite.Y =
      add(selectorSprite.Y, environment.ItemFrames.Positions[3][1]);
  selectorSprite.Color[3] = phase <= 5 ? 128 : 255;
  ProfileItemContents result;
  result.Selector = ResolveUiSprite({ProfileTextureSection::Interface, -1, 19},
                                    selectorSprite);
  const float scaled = float(baseFontSize) * content.LabelScale;
  const uint32_t fontSize = std::isnan(scaled) || scaled <= 0 ? 0
                            : scaled >= 4294967296.0f         ? UINT32_MAX
                                                      : uint32_t(scaled);
  for (size_t i = 0; i < 8; ++i) {
    const auto rowIndex = character.Events[(selector + i) % 10].Values[0];
    if (rowIndex < 0 || size_t(rowIndex) >= ownerRows.size())
      throw FormatError(
          "profile event references an unavailable owner item row");
    const auto& row = ownerRows[size_t(rowIndex)];
    if (row.Category < 0 || size_t(row.Category) >= content.Categories.size())
      throw FormatError("profile owner category outside authored sprite table");
    auto position = environment.ItemFrames.Positions[i];
    if (!contentOffsets.empty())
      for (size_t axis = 0; axis < 2; ++axis)
        position[axis] = add(position[axis], contentOffsets[i][axis]);
    auto sprite = content.Categories[size_t(row.Category)];
    sprite.X = add(sprite.X, position[0]);
    sprite.Y = add(sprite.Y, position[1]);
    auto& slot = result.Slots[i];
    slot.OwnerRow = rowIndex;
    slot.Category = row.Category;
    slot.Icon =
        ResolveUiSprite({ProfileTextureSection::Interface, -1, 16}, sprite);
    slot.Label.Text = row.Name;
    slot.Label.Position = {add(position[0], content.LabelOffset[0]),
                           add(position[1], content.LabelOffset[1])};
    slot.Label.Size = fontSize;
    const uint8_t tint = phase <= 5 ? 128 : 0;
    slot.Label.Color = {tint, tint, tint, 255};
  }
  return result;
}
ProfileDescriptionDraws BuildProfileDescription(
    const ProfileEnvironment& environment, int32_t phase,
    const ProfileCharacter& character, uint32_t selector,
    std::span<const ProfileItemDefinition> ownerRows, int32_t baseFontSize,
    std::u16string_view separator) {
  if (character.Events.size() != 10 || selector >= 10 ||
      environment.DescriptionPositions.size() < 3)
    throw FormatError(
        "profile description requires a valid scene/selector and three "
        "positions");
  const auto index = character.Events[(selector + 3) % 10].Values[0];
  if (index < 0 || size_t(index) >= ownerRows.size())
    throw FormatError("profile description owner row unavailable");
  const auto lines =
      FormatProfileItemDescription(ownerRows[size_t(index)].Description,
                                   separator, environment.DescriptionColumns);
  const float scaled = float(baseFontSize) * environment.DescriptionScale;
  const uint32_t size = std::isnan(scaled) || scaled <= 0 ? 0
                        : scaled >= 4294967296.0f         ? UINT32_MAX
                                                          : uint32_t(scaled);
  ProfileDescriptionDraws result;
  result.OwnerRow = index;
  const uint8_t tint = phase <= 5 ? 128 : 0;
  for (size_t i = 0; i < 3; ++i)
    result.Lines[i] = {lines[i],
                       environment.DescriptionPositions[i],
                       size,
                       {tint, tint, tint, 255}};
  return result;
}
}  // namespace Impacto::Saekano
