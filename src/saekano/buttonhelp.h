#pragma once
#include "font.h"
#include "uisprite.h"
namespace Impacto::Saekano {
struct ButtonHelpItem {int32_t Key=-1;std::u16string Text;};
struct ButtonHelpEnvironment {
  std::array<uint8_t,3> Flags{};
  std::array<int32_t,2> Viewport{};
  std::array<std::array<int32_t,4>,2> HeaderRectangles{};
  float HeaderScale=0;
  std::array<int32_t,4> CommonRectangle{},OptionsRectangle{};
  std::vector<ButtonHelpItem> CommonItems;
  std::vector<std::vector<ButtonHelpItem>> OptionsRows;
  size_t PrefixBytes=0;
  // env_help_mgr through native ca78's Options rows; later scenes have tails.
  static ButtonHelpEnvironment Decode(std::span<const uint8_t> bytes);
};
struct ButtonHelpStyle {
  std::vector<std::u16string> Keys;
  std::u16string Separator;
  float TextScale=0;
  int32_t Gap=0;
  size_t PrefixBytes=0;
  // Counted env_global prefix through c6ac, not fixed file offsets.
  static ButtonHelpStyle Decode(std::span<const uint8_t> bytes);
};
// Shared37a16/37c04 row layout/drawing. Options uses alignment5 and opacity1.
std::vector<UiTextBlit> BuildButtonHelp(std::span<const ButtonHelpItem> items,
    std::array<int32_t,4> rectangle,const ButtonHelpStyle& style,const GsfFont& font,
    uint32_t utilitySize,uint32_t nominalSize,int32_t alignment=5,float opacity=1);
}
