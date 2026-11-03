#include "titlemenu.h"

#include "../../profile_internal.h"

namespace Impacto::Profile::Saekano::TitleMenu {

void Configure() {
  EnsurePushMemberOfType("TitleMenu", LUA_TTABLE);
  GetMemberArray<uint32_t>(PortraitTextures, "PortraitTextures");
  Pop();
  Configured = true;
}

}  // namespace Impacto::Profile::Saekano::TitleMenu
