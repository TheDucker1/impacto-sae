#include "frontend.h"

#include "renderer.h"
#include "window.h"
#include "../userconfig.h"
#include "../formaterror.h"

#ifndef IMPACTO_DISABLE_IMGUI
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#ifndef IMPACTO_DISABLE_OPENGL
#include <imgui_custom/backends/imgui_impl_opengl3.h>
#endif
#ifndef IMPACTO_DISABLE_DX9
#include <imgui_impl_dx9.h>
#endif
#ifndef IMPACTO_DISABLE_VULKAN
#include <imgui_impl_vulkan.h>
#endif
#endif

namespace Impacto {

void DestroyRendererFrontend() {
#ifndef IMPACTO_DISABLE_IMGUI
  if (ImGui::GetCurrentContext()) {
    if (ImGui::GetIO().BackendRendererUserData) {
      switch (UserConfig::AdvancedSettings.ActiveRenderer) {
#ifndef IMPACTO_DISABLE_OPENGL
        case RendererType::OpenGL:
          ImGui_ImplOpenGL3_Shutdown();
          break;
#endif
#ifndef IMPACTO_DISABLE_DX9
        case RendererType::DirectX9:
          ImGui_ImplDX9_Shutdown();
          break;
#endif
#ifndef IMPACTO_DISABLE_VULKAN
        case RendererType::Vulkan:
          ImGui_ImplVulkan_Shutdown();
          break;
#endif
        default:
          break;
      }
    }
    if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }
#endif
  if (Renderer) Renderer->Shutdown();
  delete Renderer;
  Renderer = nullptr;
  if (Window) Window->Shutdown(false);
  delete Window;
  Window = nullptr;
}

RendererFrontend::RendererFrontend(std::string_view title, int width,
                                   int height, bool hidden) {
  if (Window || Renderer)
    throw FormatError("a rendering frontend is already active");
  if (width <= 0 || height <= 0)
    throw FormatError("invalid frontend dimensions");
  UserConfig::CommonSettings.ResolutionWidth = width;
  UserConfig::CommonSettings.ResolutionHeight = height;
  try {
#ifndef IMPACTO_DISABLE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
#endif
    InitWindow();
    Window->FullFramebuffer = true;
    Window->RenderScale = 1.0f;
    CreateRenderer();
    Renderer->Init();
    const std::string windowTitle(title);
    SDL_SetWindowTitle(Window->SDLWindow, windowTitle.c_str());
    SDL_SetWindowResizable(Window->SDLWindow, true);
    SDL_SetWindowAspectRatio(Window->SDLWindow, 0, 0);
    if (!SDL_SetWindowSize(Window->SDLWindow, width, height))
      throw FormatError(SDL_GetError());
    if (hidden)
      SDL_HideWindow(Window->SDLWindow);
    else
      SDL_ShowWindow(Window->SDLWindow);
  } catch (...) {
    DestroyRendererFrontend();
    throw;
  }
}

RendererFrontend::~RendererFrontend() { DestroyRendererFrontend(); }
SDL_Window* RendererFrontend::GetWindow() const { return Window->SDLWindow; }

void RendererFrontend::BeginGui() {
  if (frameStarted) throw FormatError("frontend frame already started");
  Window->Update();
  Renderer->BeginFrame();
  Renderer->BeginFrame2D();
  Renderer->Clear(glm::vec4(.065f, .085f, .12f, 1.0f));
#ifndef IMPACTO_DISABLE_IMGUI
  Renderer->ImGuiBeginFrame();
#endif
  frameStarted = true;
}

void RendererFrontend::EnsureFrame() {
  if (!frameStarted) BeginGui();
}

void RendererFrontend::Present() {
  if (!frameStarted) throw FormatError("frontend frame has not started");
  Renderer->EndFrame();
  Window->Draw();
  frameStarted = false;
}

}  // namespace Impacto
