#pragma once

#include <array>
#include <cstdint>

namespace Impacto::Profile::Saekano::TitleMenu {

inline bool Configured = false;
inline std::array<uint32_t, 4> PortraitTextures;

void Configure();

}  // namespace Impacto::Profile::Saekano::TitleMenu
