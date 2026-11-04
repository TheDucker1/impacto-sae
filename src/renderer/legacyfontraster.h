#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

// Compatible with FreeType's public outline/bitmap ABI. Font loading and
// outline generation continue to use the normal shared FreeType library.
extern "C" int ImpactoRenderLegacyFontOutline(const FT_Outline* outline,
                                              FT_Bitmap* bitmap);
