#include "dialog.h"
#include "commands.h"
#include "profile.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Impacto::Saekano {
DialogEnvironment DialogEnvironment::Decode(std::span<const uint8_t> bytes) {
  if (bytes.size() < 3 || bytes.size() > 32 * 1024 * 1024)
    throw FormatError("invalid dialog environment extent");
  DialogEnvironment result;
  std::copy_n(bytes.begin(), 3, result.Flags.begin());
  PayloadReader reader(bytes.subspan(3));
  const auto vector = [&](auto& values) {
    for (auto& v : values) v = reader.I32();
  };
  const auto count = [&](size_t size) {
    const auto n = reader.U32();
    reader.CheckCount(n, size);
    return n;
  };
  const auto color = [&]() {
    const auto v = reader.U32();
    return std::array<uint8_t, 4>{uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v),
                                  uint8_t(v >> 24)};
  };
  vector(result.HeaderWords);
  const auto frames = count(24);
  for (uint32_t i = 0; i < frames; ++i) {
    std::array<int32_t, 6> value;
    vector(value);
    result.FrameParts.push_back(value);
  }
  vector(result.Focus);
  for (auto& row : result.ButtonPositions) {
    const auto n = count(8);
    for (uint32_t i = 0; i < n; ++i) {
      std::array<int32_t, 2> v;
      vector(v);
      row.push_back(v);
    }
  }
  for (auto& v : result.Vectors) vector(v);
  for (auto& v : result.Rectangles) vector(v);
  vector(result.Offset);
  vector(result.Decoration);
  vector(result.TextExtent);
  vector(result.Settings);
  for (auto& v : result.TextScales) v = std::bit_cast<float>(reader.U32());
  result.Separator = reader.String();
  for (auto& v : result.Colors) v = color();
  const auto groups = count(4);
  for (uint32_t i = 0; i < groups; ++i) {
    const auto n = count(8);
    std::vector<DialogButtonLabels> group;
    for (uint32_t j = 0; j < n; ++j)
      group.push_back({reader.String(), reader.String()});
    result.ButtonGroups.push_back(std::move(group));
  }
  result.FinalColor = color();
  result.PrefixBytes = bytes.size() - reader.Remaining();
  return result;
}
std::u16string DecodeProfileConfirmationPrompt(std::span<const uint8_t> bytes) {
  const auto environment = ProfileEnvironment::Decode(bytes);
  PayloadReader reader(bytes.subspan(environment.PrefixBytes));
  // ec80 sprite36, eca4 vector8, ecac/ecb0 words, ecb4 UTF16 string.
  for (size_t i = 0; i < 13; ++i) reader.U32();
  return reader.String();
}
DialogContentLayout BuildDialogContentLayout(
    const DialogEnvironment& environment, std::array<int32_t, 2> center,
    uint16_t textHeight, size_t buttons, int32_t baseFontSize,
    int32_t openDuration, int32_t closeDuration) {
  if (environment.FrameParts.empty() || environment.FrameParts[0][5] <= 0 ||
      buttons > 3)
    throw FormatError("invalid dialog content layout capacity");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto sub = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
  };
  const auto integer = [](float value) {
    if (std::isnan(value)) return int32_t(0);
    if (value >= 2147483648.0f) return INT32_MAX;
    if (value <= -2147483648.0f) return INT32_MIN;
    return int32_t(value);
  };
  DialogContentLayout result;
  result.Width = environment.Settings[3];
  result.OpenDuration =
      openDuration == -1 ? environment.Settings[8] : openDuration;
  result.CloseDuration =
      closeDuration == -1 ? environment.Settings[9] : closeDuration;
  const auto buttonHeight =
      buttons ? add(integer(float(baseFontSize) * environment.TextScales[0]),
                    environment.Settings[5])
              : 0;
  result.ContentHeight = add(
      add(textHeight, add(environment.Settings[0], environment.Settings[0])),
      buttonHeight);
  if (result.Width < 0 || result.ContentHeight < 0 || result.OpenDuration < 0 ||
      result.CloseDuration < 0)
    throw FormatError("negative dialog layout extent or duration");
  const auto tileHeight = uint32_t(environment.FrameParts[0][5]);
  const uint32_t rounded =
      (uint32_t(result.ContentHeight) + tileHeight - 1) / tileHeight;
  const uint64_t body = uint64_t(rounded) * tileHeight;
  if (body > INT32_MAX)
    throw FormatError("dialog body extent exceeds signed range");
  result.BodyHeight = int32_t(body);
  const auto top = add(sub(center[1], result.BodyHeight / 2),
                       (result.BodyHeight - result.ContentHeight) / 2);
  const auto contentTop = add(top, environment.Settings[0]);
  result.PromptPosition = {
      add(sub(center[0], result.Width / 2), environment.Settings[1]),
      add(contentTop, environment.Settings[2])};
  if (buttons)
    result.ButtonBaseline =
        add(add(contentTop, textHeight), environment.Settings[5]);
  return result;
}
void BeginDialog(DialogState& state, const DialogContentLayout& layout,
                 size_t buttons, int32_t initialSelection, bool cancelEnabled,
                 bool closeOnConfirm, uint64_t now) {
  if (buttons > 3 || layout.OpenDuration < 0 || layout.CloseDuration < 0)
    throw FormatError("invalid dialog state setup");
  state = {};
  state.Visible = true;
  state.Phase = 1;
  state.ButtonCount = buttons;
  state.OpenDuration = layout.OpenDuration;
  state.CloseDuration = layout.CloseDuration;
  state.Selected = std::min(initialSelection, int32_t(buttons) - 1);
  if (state.Selected < 0) {
    state.Selected = 1;
    state.FocusEnabled = false;
  }
  state.CancelEnabled = cancelEnabled;
  state.CloseOnConfirm = closeOnConfirm;
  state.Opacity.Start(0, 0, 0, now);
}
void CloseDialog(DialogState& state) {
  if (state.Visible) state.PendingPhase = 4;
}
void UpdateDialog(DialogState& state, const DialogInput& input, uint64_t now) {
  state.UiSounds.clear();
  if (!state.Visible) return;
  if (state.PendingPhase) {
    state.Phase = *state.PendingPhase;
    state.PendingPhase.reset();
  }
  if (state.Phase == 1) {
    if (state.OpenDuration > 0) state.UiSounds.push_back(6);
    state.Opacity.Start(0, 100, state.OpenDuration, now);
    state.Phase = 2;
  }
  if (state.Phase == 2) {
    state.Opacity.Tick(now);
    if (state.Opacity.Finished()) state.Phase = 3;
  }
  if (state.Phase == 3 && state.ButtonCount) {
    const auto confirm = [&]() {
      if (state.CloseOnConfirm) {
        state.UiSounds.push_back(3);
        state.PendingPhase = 4;
      }
    };
    if ((input.Left || input.Right) && state.ButtonCount > 1) {
      state.UiSounds.push_back(2);
      if (state.FocusEnabled) {
        const auto count = int32_t(state.ButtonCount);
        if (input.Left) {
          --state.Selected;
          if (state.Selected < 0) state.Selected = count - 1;
        } else {
          ++state.Selected;
          if (state.Selected >= count) state.Selected = 0;
        }
      } else {
        state.FocusEnabled = true;
        state.Selected = input.Left ? 0 : int32_t(state.ButtonCount) - 1;
      }
    } else if (input.Accept) {
      if (state.FocusEnabled) {
        state.Result = state.Selected;
        confirm();
      }
    } else if (input.Cancel && state.CancelEnabled) {
      state.Canceled = true;
      if (state.CloseOnConfirm) state.PendingPhase = 4;
    } else {
      for (size_t i = 0; i < input.Activated.size(); ++i) {
        if (!input.Activated[i]) continue;
        if (state.ButtonCount > 1) state.Selected = int32_t(i);
        state.Result = state.Selected;
        confirm();
      }
    }
  }
  if (state.Phase == 4) {
    if (state.CloseDuration > 0 && !state.CloseOnConfirm)
      state.UiSounds.push_back(6);
    state.Opacity.Start(100, 0, state.CloseDuration, now);
    state.Opacity.Tick(now);
    state.PendingPhase = 5;
  }
  if (state.Phase == 5) {
    if (state.Opacity.Finished()) {
      state.Visible = false;
      state.Phase = 0;
      state.PendingPhase.reset();
    }
    state.Opacity.Tick(now);
  }
}
std::vector<DialogButtonDraws> BuildDialogButtons(
    const DialogEnvironment& environment, std::array<int32_t, 2> center,
    const DialogContentLayout& layout,
    std::span<const DialogButtonLabels> labels, int32_t selected,
    bool focusEnabled, int32_t progress, int32_t baseFontSize,
    const GsfFont& font, uint32_t nominalSize) {
  if (labels.size() > 3 || progress < 0 || progress > 100 || baseFontSize < 0 ||
      !std::isfinite(environment.TextScales[0]) ||
      environment.TextScales[0] < 0)
    throw FormatError("invalid dialog button inputs");
  if (labels.empty()) return {};
  const auto& positions = environment.ButtonPositions[labels.size() - 1];
  if (positions.size() < labels.size())
    throw FormatError("missing dialog button positions");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto sub = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
  };
  const auto roundedSize = [](int32_t n) {
    const float value = float(n);
    if (value >= 2147483648.0f) return INT32_MAX;
    return int32_t(value);
  };
  const auto alpha = uint8_t(progress * 255 / 100);
  const float scaled = float(baseFontSize) * environment.TextScales[0];
  const uint32_t size = scaled >= 4294967296.0f ? UINT32_MAX : uint32_t(scaled);
  const auto originX = sub(center[0], layout.Width / 2);
  const auto fw = roundedSize(environment.Focus[4]),
             fh = roundedSize(environment.Focus[5]);
  std::vector<DialogButtonDraws> result;
  for (size_t i = 0; i < labels.size(); ++i) {
    DialogButtonDraws button;
    const auto x = add(originX, positions[i][0]);
    const auto y = add(layout.ButtonBaseline, positions[i][1]);
    const auto background = [&](uint32_t entry) {
      button.Background.push_back({{ProfileTextureSection::SharedUi, -1, entry},
                                   {sub(x, fw / 2), sub(y, fh / 2), fw, fh},
                                   {environment.Focus[2], environment.Focus[3],
                                    environment.Focus[4], environment.Focus[5]},
                                   {255, 255, 255, alpha}});
    };
    if (focusEnabled && selected == int32_t(i)) {
      background(3);
      background(5);
    } else
      background(4);
    const auto width =
        MeasurePvfText(font, labels[i].Secondary, size, nominalSize);
    button.Label.Text = labels[i].Secondary;
    button.Label.Position = {sub(x, std::bit_cast<int32_t>(width >> 1)),
                             sub(y, std::bit_cast<int32_t>(size >> 1))};
    button.Label.Size = size;
    button.Label.Color = environment.Colors[1];
    button.Label.Color[3] = alpha;
    result.push_back(std::move(button));
  }
  return result;
}
std::vector<UiSpriteBlit> BuildDialogDecoration(
    const DialogEnvironment& environment, const DialogFrameDraws& frame,
    int32_t progress) {
  const auto& pattern = environment.Rectangles[0];
  if (progress < 0 || progress > 100 || frame.Extent[0] < 0 ||
      frame.Extent[1] < 0 || pattern[4] <= 0 || pattern[5] <= 0)
    throw FormatError("invalid dialog decoration extent");
  const auto columns = frame.Extent[0] / pattern[4];
  const auto rows = frame.Extent[1] / pattern[5];
  if ((uint64_t(columns) + 1) * (uint64_t(rows) + 1) > 100000)
    throw FormatError("dialog decoration tile budget exceeded");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto sub = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
  };
  const auto rounded = [](int32_t n) {
    const float v = float(n);
    return v >= 2147483648.0f ? INT32_MAX : int32_t(v);
  };
  const auto alpha = uint8_t(progress * 255 / 100);
  std::vector<UiSpriteBlit> result;
  const auto blit = [&](uint32_t entry, std::array<int32_t, 4> destination,
                        std::array<int32_t, 4> source) {
    result.push_back({{ProfileTextureSection::SharedUi, -1, entry},
                      destination,
                      source,
                      {255, 255, 255, alpha}});
  };
  auto x = add(frame.Origin[0], environment.Vectors[0][0]);
  const auto y = add(frame.Origin[1], environment.Vectors[0][1]);
  for (int32_t row = 0; row <= rows; ++row) {
    const auto h = row == rows ? sub(frame.Extent[1] % pattern[5],
                                     environment.Vectors[1][1])
                               : pattern[5];
    for (int32_t column = 0; column < columns; ++column) {
      blit(9, {x, y, rounded(pattern[4]), rounded(h)},
           {pattern[2], pattern[3], pattern[4], h});
      x = add(x, pattern[4]);
    }
    const auto w = sub(frame.Extent[0] % pattern[4], environment.Vectors[1][0]);
    blit(9, {x, y, rounded(w), rounded(h)}, {pattern[2], pattern[3], w, h});
  }
  const auto& strip = environment.Rectangles[1];
  blit(0,
       {add(frame.Origin[0], strip[0]), add(frame.Origin[1], strip[1]),
        sub(frame.Extent[0], environment.Offset[0]), environment.Offset[1]},
       {strip[2], strip[3], strip[4], strip[5]});
  const auto& ornament = environment.Decoration;
  blit(1,
       {add(frame.Origin[0], ornament[0]), add(frame.Origin[1], ornament[1]),
        rounded(ornament[4]), rounded(ornament[5])},
       {ornament[2], ornament[3], ornament[4], ornament[5]});
  return result;
}
DialogFrameDraws BuildDialogFrame(const DialogEnvironment& environment,
                                  std::array<int32_t, 2> center, int32_t width,
                                  int32_t bodyHeight, int32_t progress,
                                  std::array<uint8_t, 4> overlayColor) {
  if (environment.FrameParts.size() < 9 || width < 0 || bodyHeight < 0 ||
      progress < 0 || progress > 100 || environment.Settings[4] < 0)
    throw FormatError("invalid dialog frame layout");
  const auto& p = environment.FrameParts;
  if (p[0][5] <= 0) throw FormatError("zero dialog tile row height");
  const auto columns = uint32_t(environment.Settings[4]);
  const auto rows = uint32_t(bodyHeight / p[0][5]);
  if ((uint64_t(columns) + 2) * (uint64_t(rows) + 2) > 100000)
    throw FormatError("dialog tile count exceeds drawing budget");
  const auto add = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
  };
  const auto sub = [](int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(uint32_t(a) - uint32_t(b));
  };
  DialogFrameDraws result;
  result.Extent = {width, add(add(bodyHeight, p[0][5]), p[6][5])};
  result.Origin = {sub(center[0], width / 2),
                   sub(center[1], result.Extent[1] / 2)};
  result.Overlay.Kind = UiRenderKind::Rectangle;
  result.Overlay.Rectangle = {0, 0, 960, 544};
  result.Overlay.Color = overlayColor;
  const auto alpha = uint8_t(progress * 255 / 100);
  const auto tile = [&](size_t index, int32_t x, int32_t y) {
    const auto& part = p[index];
    UiSpriteDefinition sprite;
    sprite.X = x;
    sprite.Y = y;
    sprite.SourceX = part[2];
    sprite.SourceY = part[3];
    sprite.Width = part[4];
    sprite.Height = part[5];
    sprite.Color = {255, 255, 255, alpha};
    result.Tiles.push_back(
        ResolveUiSprite({ProfileTextureSection::SharedUi, -1, 0}, sprite));
  };
  const auto row = [&](size_t first, int32_t y) {
    auto x = result.Origin[0];
    tile(first, x, y);
    // Native body rows start their center from the TOP-left width, even when
    // the body's left tile has a different authored width.
    x = add(x, p[first == 3 ? 0 : first][4]);
    for (uint32_t i = 0; i < columns; ++i) {
      tile(first + 1, x, y);
      x = add(x, p[first + 1][4]);
    }
    tile(first + 2, x, y);
  };
  row(0, result.Origin[1]);
  auto y = add(result.Origin[1], p[2][5]);
  for (uint32_t i = 0; i < rows; ++i) {
    row(3, y);
    y = add(y, p[5][5]);
  }
  row(6, y);
  return result;
}
}  // namespace Impacto::Saekano
