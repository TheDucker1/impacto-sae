#pragma once

#include <SDL3/SDL.h>
#include <string_view>

namespace Impacto {

// Owns the shared window, renderer and ImGui frontend for a standalone screen.
// Game logic and audio remain with the caller.
class RendererFrontend {
 public:
  RendererFrontend(std::string_view title, int width, int height,
                   bool hidden = false);
  ~RendererFrontend();
  RendererFrontend(const RendererFrontend&) = delete;
  RendererFrontend& operator=(const RendererFrontend&) = delete;

  SDL_Window* GetWindow() const;
  void BeginGui();
  void EnsureFrame();
  void Present();

 private:
  bool frameStarted = false;
};

void DestroyRendererFrontend();

}  // namespace Impacto
