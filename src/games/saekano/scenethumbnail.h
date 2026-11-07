#pragma once
#include "slotdata.h"

namespace Impacto::Saekano {
// Native b6190..b61b2, with explicit RGBA input instead of GXM's BGRA bytes.
// Input is192x108, top row first. Alpha is discarded; color channels truncate.
SlotThumbnail PackSceneThumbnailRgba(std::span<const uint8_t> rgba);
}
