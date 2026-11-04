# FreeType legacy grayscale rasterizer

Unmodified standalone grayscale scan converter and image types from the official
FreeType VER-2-4-12 release:
https://github.com/freetype/freetype/tree/VER-2-4-12

Files: src/smooth/ftgrays.c, src/smooth/ftgrays.h,
include/freetype/ftimage.h, docs/FTL.TXT (stored as LICENSE.TXT).

Used under the FreeType License in LICENSE.TXT. This software is based in part on
the work of the FreeType Team. The source copyright headers are retained.

Only the scan converter is compiled, through src/renderer/legacyfontraster.c.
The normal FreeType dependency still loads and sizes fonts and produces outlines.
The wrapper supplies standalone macros, isolates symbols from the current
FreeType library, and uses a separate raster workspace for each call.
This preserves older grayscale coverage without changing other text renderers.
