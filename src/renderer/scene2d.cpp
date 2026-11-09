#include "scene2d.h"
#include "renderer.h"
#include "../userconfig.h"
#include "../formaterror.h"

namespace Impacto {
#ifndef IMPACTO_DISABLE_OPENGL
namespace OpenGL {
std::unique_ptr<Scene2D> CreateScene2DPass(std::array<int, 2> canvasSize);
}
#endif
#ifndef IMPACTO_DISABLE_DX9
namespace DirectX9 {
std::unique_ptr<Scene2D> CreateScene2DPass(std::array<int, 2> canvasSize);
}
#endif
std::unique_ptr<Scene2D> CreateScene2D(std::array<int, 2> canvasSize) {
  if (canvasSize[0] <= 0 || canvasSize[1] <= 0)
    throw FormatError("invalid scene canvas size");
  switch (UserConfig::AdvancedSettings.ActiveRenderer) {
#ifndef IMPACTO_DISABLE_OPENGL
    case RendererType::OpenGL:
      return OpenGL::CreateScene2DPass(canvasSize);
#endif
#ifndef IMPACTO_DISABLE_DX9
    case RendererType::DirectX9:
      return DirectX9::CreateScene2DPass(canvasSize);
#endif
    default:
      throw FormatError("the selected renderer has no legacy scene pass");
  }
}
}  // namespace Impacto
