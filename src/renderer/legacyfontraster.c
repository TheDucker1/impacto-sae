#define _STANDALONE_
#define FT_ABS(x) ((x) < 0 ? -(x) : (x))
#define ft_grays_raster ImpactoLegacyGrayRasterFuncs
#define FT_Outline_Decompose ImpactoLegacyOutlineDecompose
#include "../../vendor/freetype-legacy-raster/ftgrays.c"

int ImpactoRenderLegacyFontOutline(const FT_Outline* outline,
                                   FT_Bitmap* bitmap) {
  gray_TRaster raster;
  long pool[8192];
  FT_Raster_Params params;
  memset(&raster, 0, sizeof(raster));
  memset(&params, 0, sizeof(params));
  // The standalone factory uses a global object. A call-local workspace keeps
  // independent font owners from sharing mutable raster state.
  gray_raster_reset((FT_Raster)&raster, (char*)pool, sizeof(pool));
  params.target = bitmap;
  params.source = outline;
  params.flags = FT_RASTER_FLAG_AA;
  return gray_raster_render(&raster, &params);
}
