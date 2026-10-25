#include "desktop.h"

#include "live2dsdk.h"
#include "modelscene.h"
#include "resources.h"
#include "uisprite.h"
#include "profilewave.h"
#include "profile.h"
#include "profileitems.h"
#include "fontface.h"
#include "../io/memorystream.h"
#include "../texture/texture.h"

#include <SDL3/SDL.h>
#include <glad/glad.h>

#include <algorithm>
#include <bit>
#include <fstream>
#include <iterator>
#include <string>
#include <map>

namespace Impacto::Saekano {
namespace {
void SdlCheck(bool ok, const char* operation) {
  if (!ok)
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
void GlCheck(const char* operation) {
  const auto error = glGetError();
  if (error != GL_NO_ERROR)
    throw std::runtime_error(std::string(operation) + ": OpenGL error " +
                             std::to_string(error));
}
struct ViewerWindow {
  SDL_Window* Window = nullptr;
  SDL_GLContext Context = nullptr;
  explicit ViewerWindow(bool hidden, bool stencil = false) {
    SdlCheck(SDL_Init(SDL_INIT_VIDEO), "SDL initialization");
    try {
      SdlCheck(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3),
               "GL major version");
      SdlCheck(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3),
               "GL minor version");
      // Cubism2 supplies its legacy shader pipeline. Keep this separate from
      // the SC3 frontend's core-profile context and renderer state caches.
      SdlCheck(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                   SDL_GL_CONTEXT_PROFILE_COMPATIBILITY),
               "GL profile");
      SdlCheck(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "GL double buffer");
      if (stencil) {
        SdlCheck(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24), "depth size");
        SdlCheck(SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8), "stencil size");
      }
      const auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                         (hidden ? SDL_WINDOW_HIDDEN : 0);
      Window = SDL_CreateWindow("Saekano model viewer", 1000, 800, flags);
      SdlCheck(Window != nullptr, "window creation");
      Context = SDL_GL_CreateContext(Window);
      SdlCheck(Context != nullptr, "GL context creation");
      if (!gladLoadGLLoader(
              reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
        throw std::runtime_error("cannot load OpenGL functions");
      SDL_GL_SetSwapInterval(hidden ? 0 : 1);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      GlCheck("viewer initialization");
    } catch (...) {
      Cleanup();
      throw;
    }
  }
  void Cleanup() {
    if (Context) SDL_GL_DestroyContext(Context);
    if (Window) SDL_DestroyWindow(Window);
    SDL_Quit();
  }
  ~ViewerWindow() { Cleanup(); }
};

struct ModelTextures {
  std::vector<uint32_t> Handles;
  ModelTextures(Cubism2Model& model, const Live2dModelAssets& assets) {
    try {
      const auto& textures = assets.Get(Live2dSection::Textures);
      for (size_t index = 0; index < textures.size(); ++index) {
        Texture texture;
        const auto& data = textures[index].Data;
        Io::MemoryStream stream(const_cast<uint8_t*>(data.data()), data.size());
        if (!texture.Load(&stream))
          throw FormatError("cannot decode model GXT texture");
        if (texture.Format != TexFmt_RGBA)
          throw FormatError("unsupported model texture pixel format");
        GLuint handle = 0;
        glGenTextures(1, &handle);
        Handles.push_back(handle);
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.Width, texture.Height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, texture.Buffer.data());
        model.BindTexture(index, handle);
      }
      GlCheck("model textures");
    } catch (...) {
      Release();
      throw;
    }
  }
  void Release() {
    if (!Handles.empty())
      glDeleteTextures(static_cast<GLsizei>(Handles.size()), Handles.data());
    Handles.clear();
  }
  ~ModelTextures() { Release(); }
};

struct ViewerModel {
  std::unique_ptr<Cubism2Model> Model;
  std::unique_ptr<ModelTextures> Textures;
  int IdleHandle = -1;
  size_t IdleMotion = 0;
  std::vector<size_t> Expressions;
  size_t ExpressionIndex = 0;
  std::string Name;
  ViewerModel(Cubism2Sdk& sdk, const Live2dModelAssets& assets) {
    const auto& mocs = assets.Get(Live2dSection::Moc);
    if (mocs.size() != 1) throw FormatError("expected one MOC per model");
    const auto& textureAssets = assets.Get(Live2dSection::Textures);
    Model = sdk.LoadModel(mocs[0].Data, textureAssets.size());
    Name.assign(assets.Name.begin(), assets.Name.end());
    try {
      Textures = std::make_unique<ModelTextures>(*Model, assets);
      const auto& motions = assets.Get(Live2dSection::LoopMotions);
      if (motions.empty())
        throw FormatError("model has no authored loop motions");
      IdleMotion = Model->LoadMotion(motions[0].Data, true);
      IdleHandle = Model->StartMotion(IdleMotion, 0);
      const auto& poses = assets.Get(Live2dSection::Pose);
      if (poses.size() != 1) throw FormatError("expected one model pose file");
      Model->LoadPose(poses[0].Data);
      for (const auto& expression : assets.Get(Live2dSection::Expressions))
        Expressions.push_back(Model->LoadExpression(expression.Data));
      if (!Expressions.empty()) Model->StartExpression(Expressions[0]);
      // Establish the SDK queue's start time before the first timed frame.
      Model->RestoreParameters();
      Model->ApplyMotionChannel(0);
      Model->SaveParameters();
      Model->ApplyExpression();
      Model->ApplyPose();
      Model->Update();
      Model->SetMatrix({2.0f / Model->CanvasWidth(), 0, 0, 0, 0,
                        -2.0f / Model->CanvasHeight(), 0, 0, 0, 0, 1, 0, -1, 1,
                        0, 1});
      Model->SetColor(1, 1, 1, 1);
      GlCheck("model textures");
    } catch (...) {
      Release();
      throw;
    }
  }
  void Release() {
    Model.reset();
    Textures.reset();
  }
  ~ViewerModel() { Release(); }
  std::string Title() const {
    return Name + " | expression " + std::to_string(ExpressionIndex) +
           " | Left/Right: model | Up/Down: expression | Escape: close";
  }
  void SelectExpression(int direction) {
    if (Expressions.empty()) return;
    ExpressionIndex =
        (ExpressionIndex + Expressions.size() + direction) % Expressions.size();
    Model->StartExpression(Expressions[ExpressionIndex]);
  }
  void Draw(int width, int height) {
    glViewport(0, 0, width, height);
    glClearColor(32.0f / 255, 32.0f / 255, 32.0f / 255, 1);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glDepthMask(GL_TRUE);
    glClearDepth(1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto scale = std::min(float(width) / Model->CanvasWidth(),
                                float(height) / Model->CanvasHeight());
    const int viewWidth = int(Model->CanvasWidth() * scale);
    const int viewHeight = int(Model->CanvasHeight() * scale);
    glViewport((width - viewWidth) / 2, (height - viewHeight) / 2, viewWidth,
               viewHeight);
    if (Model->MotionFinished(0, IdleHandle))
      IdleHandle = Model->StartMotion(IdleMotion, 0);
    Model->RestoreParameters();
    Model->ApplyMotionChannel(0);
    Model->SaveParameters();
    Model->ApplyExpression();
    Model->ApplyPose();
    Model->Update();
    Model->Draw();
    GlCheck("model draw");
  }
};

void WriteUiBlit(std::ostream& out, const UiSpriteBlit& blit) {
  out << "{\"texture\":[" << uint32_t(blit.Texture.Section) << ','
      << blit.Texture.Character << ',' << blit.Texture.Entry
      << "],\"destination\":[";
  for (size_t j = 0; j < 4; ++j) {
    if (j) out << ',';
    out << blit.Destination[j];
  }
  out << "],\"source\":[";
  for (size_t j = 0; j < 4; ++j) {
    if (j) out << ',';
    out << blit.Source[j];
  }
  out << "],\"color\":[";
  for (size_t j = 0; j < 4; ++j) {
    if (j) out << ',';
    out << unsigned(blit.Color[j]);
  }
  out << "]}";
}
void WriteUiCommand(std::ostream& out, const UiRenderCommand& command) {
  if (command.Kind == UiRenderKind::Sprite) {
    WriteUiBlit(out, command.Sprite);
    return;
  }
  out << "{\"kind\":" << int(command.Kind) << ",\"mode\":" << command.Mode;
  if (command.Kind == UiRenderKind::Rectangle) {
    out << ",\"rectangle\":[";
    for (size_t i = 0; i < 4; ++i) {
      if (i) out << ',';
      out << command.Rectangle[i];
    }
    out << "],\"color\":[";
    for (size_t i = 0; i < 4; ++i) {
      if (i) out << ',';
      out << unsigned(command.Color[i]);
    }
    out << ']';
  }
  out << '}';
}
void WriteItemLabel(std::ostream& out, const ProfileItemSlotDraw& slot) {
  out << "{\"row\":" << slot.OwnerRow << ",\"category\":" << slot.Category
      << ",\"text\":[";
  for (size_t i = 0; i < slot.Label.Text.size(); ++i) {
    if (i) out << ',';
    out << uint16_t(slot.Label.Text[i]);
  }
  out << "],\"position\":[" << slot.Label.Position[0] << ','
      << slot.Label.Position[1] << "],\"size\":" << slot.Label.Size
      << ",\"color\":[";
  for (size_t i = 0; i < 4; ++i) {
    if (i) out << ',';
    out << unsigned(slot.Label.Color[i]);
  }
  out << "]}";
}
struct UiBlitRenderer {
  GLuint Program = 0, Vertex = 0, Fragment = 0, Vao = 0, Buffer = 0;
  GLint Tint = -1, Solid = -1, AlphaMode = -1;
  struct BoundTexture {
    GLuint Handle;
    uint32_t Width, Height;
  };
  std::map<ProfileTextureKey, BoundTexture> Textures;
  static GLuint Compile(GLenum type, const char* source) {
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[1024]{};
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      glDeleteShader(shader);
      throw FormatError(std::string("UI shader: ") + log);
    }
    return shader;
  }
  UiBlitRenderer() {
    try {
      Vertex = Compile(GL_VERTEX_SHADER, R"GLSL(#version 330
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
out vec2 texturePosition;
void main(){texturePosition=uv;gl_Position=vec4(position.x/480.0-1.0,1.0-position.y/272.0,0.0,1.0);}
)GLSL");
      Fragment = Compile(GL_FRAGMENT_SHADER, R"GLSL(#version 330
in vec2 texturePosition;
uniform sampler2D image;
uniform vec4 tint;
uniform bool solid;
uniform int alphaMode;
out vec4 outputColor;
void main(){
  outputColor=solid?tint:texture(image,texturePosition)*tint;
  if((alphaMode==6 && outputColor.a<=0.0)||(alphaMode==3 && outputColor.a==0.0))discard;
}
)GLSL");
      Program = glCreateProgram();
      glAttachShader(Program, Vertex);
      glAttachShader(Program, Fragment);
      glLinkProgram(Program);
      GLint ok = 0;
      glGetProgramiv(Program, GL_LINK_STATUS, &ok);
      if (!ok) throw FormatError("UI shader program link failed");
      glDeleteShader(Vertex);
      Vertex = 0;
      glDeleteShader(Fragment);
      Fragment = 0;
      glGenVertexArrays(1, &Vao);
      glGenBuffers(1, &Buffer);
      glBindVertexArray(Vao);
      glBindBuffer(GL_ARRAY_BUFFER, Buffer);
      glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), nullptr,
                   GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            nullptr);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            reinterpret_cast<void*>(2 * sizeof(float)));
      glUseProgram(Program);
      glUniform1i(glGetUniformLocation(Program, "image"), 0);
      Tint = glGetUniformLocation(Program, "tint");
      Solid = glGetUniformLocation(Program, "solid");
      AlphaMode = glGetUniformLocation(Program, "alphaMode");
      GlCheck("UI renderer initialization");
    } catch (...) {
      Release();
      throw;
    }
  }
  void Upload(ProfileTextureKey key, const ImagePixels& pixels) {
    if (Textures.contains(key))
      throw FormatError("duplicate UI texture binding");
    GLuint handle = 0;
    glGenTextures(1, &handle);
    Textures.emplace(key, BoundTexture{handle, pixels.Width, pixels.Height});
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.Width, pixels.Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.Rgba.data());
    GlCheck("profile UI texture upload");
  }
  void Draw(std::span<const UiSpriteBlit> blits, int width, int height,
            std::span<const UiRenderCommand> commands = {}) {
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glDepthMask(GL_TRUE);
    glClearDepth(1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto scale = std::min(float(width) / 960, float(height) / 544);
    const int w = int(960 * scale), h = int(544 * scale);
    glViewport((width - w) / 2, (height - h) / 2, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    // Same normal sprite blend policy as Impacto's OpenGL renderer.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    glUseProgram(Program);
    glBindVertexArray(Vao);
    glBindBuffer(GL_ARRAY_BUFFER, Buffer);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(AlphaMode, 0);
    for (const auto& blit : blits)
      DrawQuad(blit, Textures.at(blit.Texture), false);
    if (!commands.empty()) {
      GLint bits = 0;
      glGetIntegerv(GL_STENCIL_BITS, &bits);
      if (bits < 8)
        throw FormatError(
            "profile item pass requires an eight-bit stencil buffer");
    }
    for (const auto& command : commands) {
      switch (command.Kind) {
        case UiRenderKind::ResetDepthStencilBackground:
          glClearDepth(1);
          glClearStencil(0);
          break;
        case UiRenderKind::Stencil:
          if (command.Mode == 0) {
            glDisable(GL_STENCIL_TEST);
          } else if (command.Mode >= 1 && command.Mode <= 3) {
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xff);
            glStencilFunc(command.Mode == 3 ? GL_EQUAL : GL_ALWAYS,
                          command.Mode == 1 ? 1 : 0, 0xff);
            glStencilOp(GL_KEEP, command.Mode == 3 ? GL_KEEP : GL_REPLACE,
                        command.Mode == 3 ? GL_KEEP : GL_REPLACE);
          } else
            throw UnsupportedCommand("unknown profile stencil mode");
          break;
        case UiRenderKind::AlphaTest:
          if (command.Mode != 3 && command.Mode != 6)
            throw UnsupportedCommand("unsupported profile alpha comparison");
          glUniform1i(AlphaMode, command.Mode);
          break;
        case UiRenderKind::DepthTest:
          if (command.Mode != 0 && command.Mode != 5)
            throw UnsupportedCommand("unsupported profile depth comparison");
          glEnable(GL_DEPTH_TEST);
          glDepthMask(GL_TRUE);
          glDepthFunc(command.Mode == 0 ? GL_NEVER : GL_LEQUAL);
          break;
        case UiRenderKind::Rectangle: {
          UiSpriteBlit solid;
          solid.Destination = command.Rectangle;
          solid.Color = command.Color;
          DrawQuad(solid, {0, 1, 1}, true);
          break;
        }
        case UiRenderKind::Sprite:
          DrawQuad(command.Sprite, Textures.at(command.Sprite.Texture), false);
          break;
      }
    }
    GlCheck("profile UI drawing");
  }
  void DrawQuad(const UiSpriteBlit& blit, const BoundTexture& texture,
                bool solid) {
    glUniform1i(Solid, solid ? 1 : 0);
    if (blit.Source[2] < 0 || blit.Source[3] < 0)
      throw UnsupportedCommand(
          "mirrored UI sampling needs native backend binding");
    const float x = float(blit.Destination[0]), y = float(blit.Destination[1]);
    const float r = x + blit.Destination[2], b = y + blit.Destination[3];
    const float u = float(blit.Source[0]) / texture.Width,
                v = float(blit.Source[1]) / texture.Height;
    const float ur = (float(blit.Source[0]) + blit.Source[2]) / texture.Width;
    const float vb = (float(blit.Source[1]) + blit.Source[3]) / texture.Height;
    const std::array<float, 24> vertices{x, y, u,  v,  r, y, ur, v,
                                         x, b, u,  vb, x, b, u,  vb,
                                         r, y, ur, v,  r, b, ur, vb};
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glBindTexture(GL_TEXTURE_2D, texture.Handle);
    glUniform4f(Tint, blit.Color[0] / 255.0f, blit.Color[1] / 255.0f,
                blit.Color[2] / 255.0f, blit.Color[3] / 255.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  void Release() {
    for (const auto& [key, texture] : Textures) {
      (void)key;
      glDeleteTextures(1, &texture.Handle);
    }
    Textures.clear();
    if (Buffer) glDeleteBuffers(1, &Buffer);
    if (Vao) glDeleteVertexArrays(1, &Vao);
    if (Program) glDeleteProgram(Program);
    if (Vertex) glDeleteShader(Vertex);
    if (Fragment) glDeleteShader(Fragment);
    Buffer = Vao = Program = Vertex = Fragment = 0;
  }
  ~UiBlitRenderer() { Release(); }
};
struct FontTextures {
  std::map<std::pair<bool, uint32_t>, UiBlitRenderer::BoundTexture> Textures;
  const UiBlitRenderer::BoundTexture& Get(bool bitmap, uint32_t id,
                                          const ImagePixels& pixels) {
    const auto key = std::pair{bitmap, id};
    if (const auto found = Textures.find(key); found != Textures.end())
      return found->second;
    GLuint handle = 0;
    glGenTextures(1, &handle);
    const auto inserted = Textures.emplace(
        key, UiBlitRenderer::BoundTexture{handle, pixels.Width, pixels.Height});
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.Width, pixels.Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.Rgba.data());
    GlCheck("font texture upload");
    return inserted.first->second;
  }
  void DrawString(UiBlitRenderer& renderer, PcFontFace& font,
                  std::u16string_view text, int32_t x, int32_t y, uint32_t size,
                  std::array<uint8_t, 4> color,
                  std::ostream* report = nullptr) {
    if (size > 1024) throw FormatError("PC text size outside display budget");
    for (const auto character : text) {
      if (character == 0) break;  // Native DrawString consumes a UTF16 C string.
      UiSpriteBlit blit;
      blit.Color = color;
      const auto cached = ResolveGsfGlyph(font.Cached(), character, x, y, size);
      const bool bitmap = cached.has_value();
      uint32_t id = 0;
      const ImagePixels* pixels = nullptr;
      if (cached) {
        id = cached->Atlas;
        pixels = &font.Atlas(id);
        blit.Destination = cached->Destination;
        blit.Source = cached->Source;
      } else if ((pixels = font.Glyph(character))) {
        id = uint16_t(character);
        blit.Destination = {x, y, int32_t(PvfGlyphAdvance(character, size)),
                            int32_t(size)};
        blit.Source = {0, 0, int32_t(pixels->Width), int32_t(pixels->Height)};
      }
      if (pixels) {
        renderer.DrawQuad(blit, Get(bitmap, id, *pixels), false);
        if (report) {
          *report << "{\"character\":" << uint16_t(character)
                  << ",\"bitmap\":" << (bitmap ? "true" : "false")
                  << ",\"texture\":" << id << ",\"destination\":[";
          for (size_t i = 0; i < 4; ++i) {
            if (i) *report << ',';
            *report << blit.Destination[i];
          }
          *report << "],\"source\":[";
          for (size_t i = 0; i < 4; ++i) {
            if (i) *report << ',';
            *report << blit.Source[i];
          }
          *report << "]}\n";
        }
      }
      x = std::bit_cast<int32_t>(uint32_t(x) +
                                 PvfGlyphAdvance(character, size));
    }
    GlCheck("font text drawing");
  }
  ~FontTextures() {
    for (const auto& [key, texture] : Textures) {
      (void)key;
      glDeleteTextures(1, &texture.Handle);
    }
  }
};
void Capture(const std::filesystem::path& file, int width, int height) {
  std::vector<uint8_t> pixels(size_t(width) * size_t(height) * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  GlCheck("frame readback");
  size_t changed = 0;
  for (size_t i = 0; i < pixels.size(); i += 4)
    if (pixels[i] != 32 || pixels[i + 1] != 32 || pixels[i + 2] != 32)
      ++changed;
  if (changed < 100)
    throw std::runtime_error("model draw produced an empty frame");
  const size_t row = size_t(width) * 4;
  for (int y = 0; y < height / 2; ++y)
    std::swap_ranges(pixels.begin() + size_t(y) * row,
                     pixels.begin() + size_t(y + 1) * row,
                     pixels.begin() + size_t(height - 1 - y) * row);
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
      SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                            pixels.data(), width * 4),
      SDL_DestroySurface);
  SdlCheck(bool(surface), "capture surface");
  SdlCheck(SDL_SaveBMP(surface.get(), file.string().c_str()), "frame capture");
}
}  // namespace

int RunFontSample(const std::filesystem::path& usrdir,
                  const std::filesystem::path& captureDirectory) {
  try {
    const bool capture = !captureDirectory.empty();
    if (capture) {
      const auto output = std::filesystem::weakly_canonical(captureDirectory);
      const auto input = std::filesystem::weakly_canonical(usrdir);
      for (auto parent = output; !parent.empty();
           parent = parent.parent_path()) {
        if (parent == input || parent == input.parent_path())
          throw FormatError("font capture overlaps original data");
        if (parent == parent.parent_path()) break;
      }
      if (std::filesystem::exists(output))
        throw FormatError("choose a fresh font capture directory");
      std::filesystem::create_directories(output);
    }
    const auto read = [](const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) throw FormatError("cannot open font sample input");
      Bytes bytes{std::istreambuf_iterator<char>(file), {}};
      if (file.bad()) throw FormatError("cannot read font sample input");
      return bytes;
    };
    PcFontFace font(read(usrdir / "DFHSR4.TTC"),
                    GsfFont::Decode(read(usrdir / "Font.gsf")));
    const auto utility =
        GameUtilityFont::Decode(read(usrdir / "env/env_game_utility.dat"));
    if (utility.Size < 0 || utility.Size > 1024)
      throw FormatError("font sample UI size outside display budget");
    const auto catalog = LoadProfileItemCatalog(read(usrdir / "first.dat"));
    if (catalog.empty() || catalog.front().Rows.size() < 10)
      throw FormatError("font sample requires authored item rows");
    ViewerWindow window(capture);
    SdlCheck(SDL_SetWindowTitle(window.Window, "Saekano authored font sample"),
             "font window title");
    SdlCheck(SDL_SetWindowSize(window.Window, 960, 544), "font window size");
    UiBlitRenderer renderer;
    FontTextures textures;
    std::ofstream glyphReport;
    if (capture) {
      glyphReport.open(captureDirectory / "glyphs.jsonl");
      if (!glyphReport) throw FormatError("cannot create font glyph report");
      std::ofstream metadata(captureDirectory / "font.json");
      metadata << "{\"face\":0,\"family\":\"" << font.Family()
               << "\",\"nominal\":" << font.NominalSize()
               << ",\"point_size\":" << font.PointSize()
               << ",\"point_size_bits\":"
               << std::bit_cast<uint32_t>(font.PointSize())
               << ",\"baseline\":" << font.Baseline()
               << ",\"ui_size\":" << utility.Size
               << ",\"pvf_pixel_parity\":false,\"full_profile_ui\":false}\n";
      metadata.close();
      if (!metadata) throw FormatError("cannot save font metadata report");
    }
    bool quit = false;
    while (!quit) {
      SDL_Event event;
      while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
          quit = true;
      if (quit) break;
      int width = 0, height = 0;
      SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
               "font frame size");
      renderer.Draw({}, width, height);
      auto report = capture ? &glyphReport : nullptr;
      std::u16string symbols;
      for (const auto& [character, glyph] : font.Cached().Glyphs) {
        (void)glyph;
        symbols.push_back(character);
      }
      textures.DrawString(renderer, font, symbols, 36, 28, 36,
                          {255, 255, 255, 255}, report);
      for (size_t i = 0; i < 10; ++i)
        textures.DrawString(renderer, font, catalog.front().Rows[i].Name, 36,
                            90 + int32_t(i) * 38, uint32_t(utility.Size),
                            {240, 240, 240, 255}, report);
      if (capture) {
        Capture(captureDirectory / "0.bmp", width, height);
        glyphReport.close();
        if (!glyphReport) throw FormatError("cannot save font glyph report");
        quit = true;
      }
      SdlCheck(SDL_GL_SwapWindow(window.Window), "font frame swap");
      if (!capture) SDL_Delay(16);
    }
    return 0;
  } catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano font: %s",
                 error.what());
    return 1;
  }
}

int RunModelViewer(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& captureDirectory) {
  try {
    std::ifstream input(resourceDat, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open resource.dat");
    GameResources resources(Bytes{std::istreambuf_iterator<char>(input), {}});
    const bool capture = !captureDirectory.empty();
    ViewerWindow window(capture);
    Cubism2Sdk sdk;
    if (capture) std::filesystem::create_directories(captureDirectory);
    size_t id = 0;
    bool quit = false;
    std::unique_ptr<ViewerModel> model;
    uint64_t epoch = SDL_GetTicks();
    while (!quit) {
      if (!model) {
        sdk.SetTime(0);
        model = std::make_unique<ViewerModel>(sdk, resources.LoadLive2d(id));
        epoch = SDL_GetTicks();
        const auto title = model->Title();
        SDL_SetWindowTitle(window.Window, title.c_str());
      }
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) quit = true;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
          if (event.key.key == SDLK_ESCAPE) quit = true;
          if (model &&
              (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
            model->SelectExpression(event.key.key == SDLK_UP ? 1 : -1);
            SDL_SetWindowTitle(window.Window, model->Title().c_str());
          }
          if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_LEFT) {
            id = (id + resources.Live2dCount() +
                  (event.key.key == SDLK_RIGHT ? 1 : -1)) %
                 resources.Live2dCount();
            model.reset();
          }
        }
      }
      if (!model || quit) continue;
      int width = 0, height = 0;
      SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
               "window pixel size");
      if (width <= 0 || height <= 0) {
        SDL_Delay(10);
        continue;
      }
      sdk.SetTime(capture ? 500 : SDL_GetTicks() - epoch);
      model->Draw(width, height);
      if (capture) {
        Capture(captureDirectory / (std::to_string(id) + ".bmp"), width,
                height);
        SDL_Log("Saekano SDK drew model %u", static_cast<unsigned>(id));
        model.reset();
        if (++id == resources.Live2dCount()) quit = true;
      }
      SDL_GL_SwapWindow(window.Window);
    }
    return 0;
  } catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano viewer: %s",
                 error.what());
    return 1;
  }
}

int RunModelScenes(const std::filesystem::path& resourceDat,
                   const std::filesystem::path& environmentDat,
                   const std::filesystem::path& captureDirectory) {
  try {
    const auto read = [](const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) throw std::runtime_error("cannot open scene input");
      return Bytes{std::istreambuf_iterator<char>(file), {}};
    };
    GameResources resources(read(resourceDat));
    const auto environment = Live2dEnvironment::Decode(read(environmentDat));
    const auto scenes = FindModelSetupScenes(resources);
    if (scenes.empty()) throw FormatError("no authored model setup scenes");
    const bool capture = !captureDirectory.empty();
    ViewerWindow window(capture);
    if (capture) std::filesystem::create_directories(captureDirectory);
    struct Scene {
      // Driver releases SDK objects before their textures and GL context.
      std::vector<std::unique_ptr<ModelTextures>> Textures;
      SdkLive2dDriver Driver;
      SemanticState State;
      uint64_t Frames = 0;
      Scene(GameResources& resources, const Live2dEnvironment& environment,
            const ModelSetupScene& setup)
          : Driver(resources, environment) {
        State = RunModelSetupScene(resources, Driver, setup);
        for (const auto& record : State.Live2d.Records) {
          const auto id = *record.ResourceId;
          Textures.push_back(std::make_unique<ModelTextures>(
              Driver.Model(id), Driver.Assets(id)));
        }
      }
      void Tick(uint64_t elapsed) {
        while (Frames * 1000 / 60 <= elapsed) {
          Driver.Frame(State.Live2d, Frames * 1000 / 60);
          ++Frames;
        }
      }
    };
    size_t selected = 0;
    bool quit = false;
    std::unique_ptr<Scene> scene;
    uint64_t epoch = 0;
    while (!quit) {
      if (!scene) {
        scene =
            std::make_unique<Scene>(resources, environment, scenes[selected]);
        const auto& name =
            resources.ScriptName(size_t(scenes[selected].Script));
        const auto title =
            "Saekano model setup: " + std::string(name.begin(), name.end()) +
            " | Left/Right: scene | Escape: close";
        SDL_SetWindowTitle(window.Window, title.c_str());
        epoch = SDL_GetTicks();
      }
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) quit = true;
        if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) continue;
        if (event.key.key == SDLK_ESCAPE) quit = true;
        if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
          selected = (selected + scenes.size() +
                      (event.key.key == SDLK_RIGHT ? 1 : -1)) %
                     scenes.size();
          scene.reset();
        }
      }
      if (quit || !scene) continue;
      int width = 0, height = 0;
      SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
               "window pixel size");
      if (width <= 0 || height <= 0) {
        SDL_Delay(10);
        continue;
      }
      scene->Tick(capture ? 500 : SDL_GetTicks() - epoch);
      glViewport(0, 0, width, height);
      glClearColor(32.0f / 255, 32.0f / 255, 32.0f / 255, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      const auto scale = std::min(float(width) / 960, float(height) / 544);
      const auto viewWidth = int(960 * scale), viewHeight = int(544 * scale);
      glViewport((width - viewWidth) / 2, (height - viewHeight) / 2, viewWidth,
                 viewHeight);
      scene->Driver.Draw(scene->State.Live2d);
      GlCheck("authored model scene draw");
      if (capture) {
        const auto script = scenes[selected].Script;
        Capture(captureDirectory / (std::to_string(script) + ".bmp"), width,
                height);
        SDL_Log("Saekano VM drew model setup script %d", script);
        scene.reset();
        if (++selected == scenes.size()) quit = true;
      }
      SDL_GL_SwapWindow(window.Window);
    }
    return 0;
  } catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano model scenes: %s",
                 error.what());
    return 1;
  }
}
int RunProfileBackdropViewer(const std::filesystem::path& resourceDat,
                             const std::filesystem::path& environmentDat,
                             const std::filesystem::path& captureDirectory,
                             bool menuChrome, bool itemFrames,
                             std::optional<size_t> itemScene) {
  bool captureCreated = false;
  try {
    if (itemScene) itemFrames = true;
    if (itemFrames) menuChrome = true;
    const bool capture = !captureDirectory.empty();
    if (capture) {
      const auto output = std::filesystem::weakly_canonical(captureDirectory);
      for (const auto& input : {resourceDat, environmentDat}) {
        const auto root =
            std::filesystem::weakly_canonical(input).parent_path();
        for (auto parent = output; !parent.empty();
             parent = parent.parent_path()) {
          if (parent == root)
            throw FormatError("backdrop capture overlaps input data");
          if (parent == parent.parent_path()) break;
        }
      }
      if (std::filesystem::exists(output))
        throw FormatError("choose a fresh backdrop capture directory");
      std::filesystem::create_directories(output);
      captureCreated = true;
    }
    const auto read = [](const std::filesystem::path& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) throw FormatError("cannot open profile drawing input");
      Bytes result{std::istreambuf_iterator<char>(file), {}};
      if (file.bad()) throw FormatError("cannot read profile drawing input");
      return result;
    };
    GameResources resources(read(resourceDat));
    const auto environment =
        ProfileBackdropEnvironment::Decode(read(environmentDat));
    std::optional<ProfileEnvironment> chromeEnvironment;
    if (menuChrome)
      chromeEnvironment = ProfileEnvironment::Decode(read(environmentDat));
    ProfileScene authoredScene;
    std::vector<ProfileItemCharacterTable> catalog;
    std::unique_ptr<PcFontFace> font;
    std::optional<GameUtilityFont> utility;
    if (itemScene) {
      const auto usrdir = resourceDat.parent_path();
      authoredScene = resources.LoadProfileScene(*itemScene);
      catalog = LoadProfileItemCatalog(read(usrdir / "first.dat"));
      font = std::make_unique<PcFontFace>(
          read(usrdir / "DFHSR4.TTC"),
          GsfFont::Decode(read(usrdir / "Font.gsf")));
      utility =
          GameUtilityFont::Decode(read(usrdir / "env/env_game_utility.dat"));
      if (authoredScene.empty() || authoredScene.size() > catalog.size())
        throw FormatError(
            "profile item scene has no matching authored catalog");
    }
    ViewerWindow window(capture, itemFrames);
    SdlCheck(SDL_SetWindowTitle(window.Window, "Saekano"), "window title");
    SdlCheck(SDL_SetWindowSize(window.Window, 960, 544), "window size");
    UiBlitRenderer renderer;
    FontTextures fontTextures;
    for (const auto id : {2u, 22u, 23u}) {
      const ProfileTextureKey key{ProfileTextureSection::Interface, -1, id};
      renderer.Upload(key,
                      DecodeSceneImage(resources.LoadProfileTexture(key).Data));
    }
    if (menuChrome) {
      for (const auto id : {3u, 11u, 17u, 24u}) {
        const ProfileTextureKey key{ProfileTextureSection::Interface, -1, id};
        renderer.Upload(
            key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
      }
    }
    if (itemFrames) {
      const ProfileTextureKey key{ProfileTextureSection::Interface, -1, 18};
      renderer.Upload(key,
                      DecodeSceneImage(resources.LoadProfileTexture(key).Data));
    }
    if (itemScene) {
      for (const auto id : {16u, 19u}) {
        const ProfileTextureKey key{ProfileTextureSection::Interface, -1, id};
        renderer.Upload(
            key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
      }
    }
    ProfileWaveAnimation wave;
    wave.Begin(environment.WavePeriod, 0);
    std::array<uint64_t, 3> times{0, 1147, 8001};
    ProfileSelectionState selection;
    if (itemFrames) {
      // Explicit single-character diagnostic boundary. These preview inputs
      // do not initialize a new game or provide its persistent owner state.
      ProfileOwnerState previewOwner;
      previewOwner.Known = true;
      BeginProfileSelection(selection, previewOwner,
                            itemScene ? authoredScene : ProfileScene(1));
      if (capture) {
        selection.Phase = 6;
        times = {0, uint64_t(chromeEnvironment->Carousel) / 2,
                 uint64_t(chromeEnvironment->Carousel)};
      }
    }
    size_t selected = 0;
    std::ofstream report;
    if (capture) {
      report.open(captureDirectory / "frames.jsonl");
      if (!report) throw FormatError("cannot create profile frame report");
    }
    const auto epoch = SDL_GetTicks();
    bool quit = false;
    while (!quit) {
      SDL_Event event;
      ProfileSelectionInput input;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
          quit = true;
        if (itemFrames && event.type == SDL_EVENT_KEY_DOWN &&
            !event.key.repeat) {
          if (event.key.key == SDLK_UP) input.EventDirection = true;
          if (event.key.key == SDLK_DOWN) input.EventDirection = false;
          if (itemScene && event.key.key == SDLK_LEFT)
            input.CharacterDirection = true;
          if (itemScene && event.key.key == SDLK_RIGHT)
            input.CharacterDirection = false;
        }
      }
      if (quit) break;
      const uint64_t now = capture ? times[selected] : SDL_GetTicks() - epoch;
      if (itemFrames) {
        if (capture && selected == 0) input.EventDirection = true;
        UpdateProfileSelection(selection, *chromeEnvironment, input, now);
      }
      wave.Update(now);
      int width = 0, height = 0;
      SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
               "frame size");
      auto blits = BuildProfileBackdrop(environment, wave.Current);
      const int32_t phase =
          itemFrames
              ? selection.Phase
              : (capture ? std::array<int32_t, 3>{5, 6, 12}[selected] : 5);
      const int32_t slide = capture && selected == 2 ? 137 : 0;
      if (menuChrome) {
        const auto chrome =
            BuildProfileMenuChrome(chromeEnvironment->Chrome, phase, slide);
        blits.insert(blits.end(), chrome.begin(), chrome.end());
      }
      std::vector<UiRenderCommand> commands;
      if (itemFrames)
        commands =
            BuildProfileItemFramePass(chromeEnvironment->ItemFrames, phase, 960,
                                      544, selection.FrameOffsets);
      renderer.Draw(blits, width, height, commands);
      std::optional<ProfileItemContents> itemContents;
      if (itemScene) {
        std::array<std::array<int32_t, 2>, 8> offsets;
        for (size_t i = 0; i < 8; ++i)
          offsets[i] = {selection.CarouselX[i].Current,
                        selection.CarouselY[i].Current};
        const auto ordinal = selection.Ordinal;
        itemContents = BuildProfileItemContents(
            *chromeEnvironment, phase, authoredScene.at(ordinal),
            selection.Selectors.at(ordinal), catalog.at(ordinal).Rows,
            utility->Size, offsets);
        renderer.DrawQuad(itemContents->Selector,
                          renderer.Textures.at(itemContents->Selector.Texture),
                          false);
        for (const auto& slot : itemContents->Slots) {
          renderer.DrawQuad(slot.Icon, renderer.Textures.at(slot.Icon.Texture),
                            false);
          fontTextures.DrawString(
              renderer, *font, slot.Label.Text, slot.Label.Position[0],
              slot.Label.Position[1], slot.Label.Size, slot.Label.Color);
        }
        // Native506b6..506de restores alpha/depth enables and stencil mode0.
        glUniform1i(renderer.AlphaMode, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        GlCheck("profile item content pass");
      }
      if (capture) {
        const auto filename = std::to_string(selected) + ".bmp";
        Capture(captureDirectory / filename, width, height);
        report << "{\"frame\":" << selected << ",\"file\":\"" << filename
               << "\",\"now\":" << now << ",\"wave\":" << wave.Current
               << ",\"epoch\":" << wave.Epoch << ",\"sample\":" << wave.Sample
               << ",\"width\":" << width << ",\"height\":" << height
               << ",\"draws\":[";
        for (size_t i = 0; i < blits.size(); ++i) {
          if (i) report << ',';
          WriteUiBlit(report, blits[i]);
        }
        report << ']';
        if (menuChrome)
          report << ",\"phase\":" << phase << ",\"result_slide\":" << slide;
        if (itemFrames) {
          report << ",\"selector\":" << selection.Selectors[selection.Ordinal]
                 << ",\"pending\":" << selection.PendingPhase.value_or(-1)
                 << ",\"frame_offsets\":[";
          for (size_t i = 0; i < 8; ++i) {
            if (i) report << ',';
            report << '[' << selection.FrameOffsets[i][0] << ','
                   << selection.FrameOffsets[i][1] << ']';
          }
          report << "]";
          const auto stencilName = std::to_string(selected) + ".stencil";
          std::vector<uint8_t> stencil(size_t(width) * size_t(height));
          glPixelStorei(GL_PACK_ALIGNMENT, 1);
          glReadPixels(0, 0, width, height, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE,
                       stencil.data());
          glPixelStorei(GL_PACK_ALIGNMENT, 4);
          GlCheck("profile stencil readback");
          std::ofstream stencilFile(captureDirectory / stencilName,
                                    std::ios::binary);
          stencilFile.write(reinterpret_cast<const char*>(stencil.data()),
                            stencil.size());
          stencilFile.close();
          if (!stencilFile)
            throw FormatError("cannot save profile stencil report");
          report << ",\"stencil_file\":\"" << stencilName
                 << "\",\"item_frame_commands\":[";
          for (size_t i = 0; i < commands.size(); ++i) {
            if (i) report << ',';
            WriteUiCommand(report, commands[i]);
          }
          report << ']';
        }
        if (itemContents) {
          report << ",\"scene\":" << *itemScene
                 << ",\"ordinal\":" << selection.Ordinal
                 << ",\"owner_rows\":\"authored_catalog_snapshot\",\"content_"
                    "offsets\":[";
          for (size_t i = 0; i < 8; ++i) {
            if (i) report << ',';
            report << '[' << selection.CarouselX[i].Current << ','
                   << selection.CarouselY[i].Current << ']';
          }
          report << "],\"item_content_draws\":[";
          WriteUiBlit(report, itemContents->Selector);
          for (const auto& slot : itemContents->Slots) {
            report << ',';
            WriteUiBlit(report, slot.Icon);
            report << ',';
            WriteItemLabel(report, slot);
          }
          report << ']';
        }
        report << "}\n";
        if (++selected == times.size()) quit = true;
      }
      SdlCheck(SDL_GL_SwapWindow(window.Window), "frame swap");
      if (!capture) SDL_Delay(16);
    }
    if (capture) {
      report.close();
      if (!report) throw FormatError("cannot save profile frame report");
    }
    return 0;
  } catch (const std::exception& error) {
    if (captureCreated) {
      std::ofstream report(captureDirectory / "error.txt");
      report << std::string(error.what()).substr(0, 2048) << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano backdrop: %s",
                 error.what());
    return 1;
  }
}
int RunProfileItemsViewer(const std::filesystem::path& usrdir, size_t scene,
                          const std::filesystem::path& captureDirectory) {
  return RunProfileBackdropViewer(usrdir / "resource.dat",
                                  usrdir / "env/env_otaku_mode.dat",
                                  captureDirectory, true, true, scene);
}
}  // namespace Impacto::Saekano
