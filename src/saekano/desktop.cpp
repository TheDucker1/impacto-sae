#include "desktop.h"

#include "live2dsdk.h"
#include "modelscene.h"
#include "resources.h"
#include "uisprite.h"
#include "profilewave.h"
#include "profilearrow.h"
#include "profile.h"
#include "profileitems.h"
#include "fontface.h"
#include "dialog.h"
#include "textreveal.h"
#include "profilearchive.h"
#include "profilemode.h"
#include "musicplayback.h"
#include "scenemessage.h"
#include "messagedraw.h"
#include "scenequeue.h"
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
void WriteDescription(std::ostream& out, int32_t row, const UiTextBlit& line) {
  out<<"{\"row\":"<<row<<",\"text\":[";
  for(size_t i=0;i<line.Text.size();++i){if(i)out<<',';out<<uint16_t(line.Text[i]);}
  out<<"],\"position\":["<<line.Position[0]<<','<<line.Position[1]<<"],\"size\":"<<line.Size<<",\"color\":[";
  for(size_t i=0;i<4;++i){if(i)out<<',';out<<unsigned(line.Color[i]);}
  out<<"]}";
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
    Execute(commands);
  }
  void Execute(std::span<const UiRenderCommand> commands) {
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
    const auto quad = ResolveUiQuad(blit, texture.Width, texture.Height);
    std::array<float, 24> vertices;
    constexpr std::array<size_t, 6> corners{0, 1, 2, 2, 1, 3};
    for (size_t i = 0; i < corners.size(); ++i)
      std::copy(quad[corners[i]].begin(), quad[corners[i]].end(),
                vertices.begin() + i * 4);
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
Bytes SceneRead(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw FormatError("cannot open scene input: " + path.string());
  return Bytes{std::istreambuf_iterator<char>(file), {}};
}
// A bounded first-message frontend. Unbound auxiliary/effect passes are
// reported explicitly; this is not yet the complete scenario renderer.
int RunCompletedScene(ViewerWindow& window, UiBlitRenderer& renderer,
                      FontTextures& fontTextures, PcFontFace& font,
                      GameResources& resources, MusicPlayback& playback,
                      ArchiveBgmResources& music, ArchiveSeResources& sounds,
                      SemanticState&& owner, const ProfileMode& completed,
                      const std::filesystem::path& root,
                      const std::filesystem::path& captureDirectory = {}) {
  const bool capture = !captureDirectory.empty();
  if (!owner.WindowOpacity || !owner.ReadColorEnabled || !owner.TextSpeed)
    throw UnsupportedCommand("scene owner display settings are missing");
  const auto opacity = *owner.WindowOpacity;
  const auto format = TextFormatEnvironment::Decode(
      SceneRead(root / "env/env_string_format.dat"), true);
  const auto timing = TextTimingEnvironment::Decode(
      SceneRead(root / "env/env_config.dat"), true);
  const auto visual = OrdinaryTextEnvironment::Decode(
      SceneRead(root / "env/env_message_window.dat"));
  const auto cursorEnvironment = OrdinaryCursorEnvironment::Decode(
      SceneRead(root / "env/env_message_window.dat"));
  ProfileArrowMotion messageCursor;
  messageCursor.Begin(cursorEnvironment.Travel, cursorEnvironment.Duration, 0);
  const auto metrics =
      TextLayoutMetrics::Decode(SceneRead(root / "env/env_global.dat"),
                                SceneRead(root / "env/env_message_window.dat"));
  const auto utility =
      GameUtilityFont::Decode(SceneRead(root / "env/env_game_utility.dat"));
  ArchiveSceneImages images(resources);
  ArchiveCharacterPoses poses(resources);
  SceneMessageBackend backend(format, timing, *owner.TextSpeed);
  backend.SceneImages = &images;
  backend.CharacterPoses = &poses;
  backend.MusicResources = &music;
  backend.SeArchives = &sounds;
  auto vm = VirtualMachine::FromCompletedProfile(resources, backend,
                                                 std::move(owner), completed);
  const auto seed = capture ? 0u : uint32_t(SDL_GetTicks());
  vm->SeedRandom(seed);
  SDL_SetWindowTitle(window.Window, "Saekano — first scene preview");
  std::ofstream report, pcm, events;
  if (capture) {
    report.open(captureDirectory / "scene-frames.jsonl");
    pcm.open(captureDirectory / "scene-output.pcm", std::ios::binary);
    events.open(captureDirectory / "scene-events.jsonl");
    if (!report || !pcm || !events)
      throw FormatError("cannot create scene capture output");
  }
  const auto drawSprite = [&](const UiSpriteBlit& sprite) {
    renderer.DrawQuad(sprite, renderer.Textures.at(sprite.Texture), false);
  };
  const auto upload = [&](ProfileTextureKey key, const ImagePixels& pixels) {
    if (!renderer.Textures.contains(key)) renderer.Upload(key, pixels);
  };
  const auto uploadMain = [&](uint32_t id) {
    const ProfileTextureKey key{ProfileTextureSection::MainUi, -1, id};
    if (!renderer.Textures.contains(key))
      upload(key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
  };
  for (auto id : {0u, 1u, 2u, 3u, 9u}) uploadMain(id);
  PlainTextLayout layout;
  bool laidOut = false, quit = false, advanced = false;
  uint64_t previous = 0, firstMessage = UINT64_MAX, lastSaved = 0;
  const auto epoch = SDL_GetTicks();
  std::string previousSignature;
  size_t saved = 0;
  std::map<uint16_t, bool> actorEligibility;
  for (uint32_t frame = 0; !quit && frame < 2000; ++frame) {
    const uint64_t now =
        capture ? uint64_t(frame) * 16 : SDL_GetTicks() - epoch;
    messageCursor.Update(now);
    CommandInput input;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT ||
          (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
        quit = true;
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
          event.key.key == SDLK_RETURN)
        input.Advance = true;
    }
    if (quit) break;
    playback.Update(float(now - previous) / 1000.0f);
    if (capture && now > previous) {
      std::vector<int16_t> samples(size_t(now - previous) * 96);
      playback.Render(samples);
      pcm.write(reinterpret_cast<const char*>(samples.data()),
                samples.size() * sizeof(int16_t));
    }
    previous = now;
    if (capture && firstMessage != UINT64_MAX && now - firstMessage >= 3000 &&
        !advanced) {
      input.Advance = true;
      advanced = true;
    }
    for (size_t work = 0; work < 256; ++work) {
      const auto step = vm->Step(now, input);
      input.Advance = false;
      if (step.Opcode == 0x64 && step.Boundary == VmBoundary::CommandStarted) {
        firstMessage = now;
        break;
      }
      if (step.Opcode == 0x64 &&
          step.Boundary == VmBoundary::CommandCompletedBeforeDispatch) {
        quit = true;
        break;
      }
      if (step.Boundary == VmBoundary::WaitingInput ||
          step.Boundary == VmBoundary::WaitingBackend)
        break;
      if (step.Boundary == VmBoundary::Ended)
        throw UnsupportedCommand("scene ended before first dialogue");
      if (work == 255)
        throw UnsupportedCommand("scene per-frame command budget exceeded");
    }
    for (const auto& request : vm->TakeEvents()) {
      playback.Submit(request);
      if (capture)
        events << "{\"kind\":\"audio\",\"frame\":" << frame
               << ",\"now\":" << now << ",\"event\":" << int(request.Kind)
               << ",\"id\":" << request.Id << "}\n";
    }
    const auto spriteClock = capture ? uint32_t(now) : uint32_t(SDL_GetTicks());
    if (capture)
      for (const auto& [id, actor] : vm->State().Characters) {
        if (!vm->SpriteBlink(id))
          events << "{\"kind\":\"blink_init\",\"frame\":" << frame
                 << ",\"id\":" << id << ",\"seed\":" << spriteClock << "}\n";
        const bool eligible =
            actor.Pose.Ready &&
            actor.Tweens[size_t(CharacterTween::Opacity)].Current != 0;
        if (!actorEligibility.contains(id) ||
            actorEligibility[id] != eligible) {
          events << "{\"kind\":\"blink_gate\",\"frame\":" << frame
                 << ",\"id\":" << id
                 << ",\"eligible\":" << (eligible ? "true" : "false") << "}\n";
          actorEligibility[id] = eligible;
        }
      }
    vm->UpdateSpriteFrames(spriteClock);
    backend.UpdateMessage(now);
    const auto& state = vm->State();
    int width = 0, height = 0;
    SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
             "scene frame size");
    renderer.Draw({}, width, height);
    const auto* message = backend.CurrentMessage();
    if (!message) {
      UiSpriteBlit white;
      white.Destination = {0, 0, 960, 544};
      white.Color = {255, 255, 255, 255};
      renderer.DrawQuad(white, {0, 1, 1}, true);
      SdlCheck(SDL_GL_SwapWindow(window.Window), "scene prelude swap");
      if (!capture) SDL_Delay(16);
      continue;
    }
    if (state.ScreenFade != 0 || state.RuleWipe.Active)
      throw UnsupportedCommand(
          "first-scene active fade/rule rendering is not bound");
    if (!state.Quake[0] || !state.Quake[1] || !state.Quake[2])
      throw UnsupportedCommand("scene quake inputs are missing");
    if (!laidOut) {
      layout =
          LayoutPlainText(message->Reveal.Text, format, font.Cached(), metrics,
                          visual.Window.TextWidth, visual.BodyScale,
                          uint32_t(utility.Size), font.NominalSize());
      laidOut = true;
    }
    SceneQueueInputs gates;
    for (size_t c = 0; c < 2; ++c)
      for (size_t i = 0; i < 4; ++i) {
        const auto id = state.SceneImages.Slots.FloatSlots[c][i].Texture;
        gates.FloatReady[c][i] =
            id >= 0 && images.AssetReady(
                           {ImageCategorySection(uint32_t(c)), uint32_t(id)});
      }
    for (const auto& slot : state.SceneImages.Slots.IntegerSlots)
      if (slot.GeometryAndTexture[6] >= 0)
        throw UnsupportedCommand("first-scene free-image drawing is not bound");
    const auto queue = BuildSceneDrawQueue(state.SceneImages.Slots, gates);
    std::vector<UiSpriteBlit> spriteDraws;
    std::vector<std::array<int32_t, 6>> widgetInputs;
    std::vector<UiTextBlit> textDraws;
    for (const auto& pass : queue) {
      if (pass.Kind == SceneDrawKind::Background ||
          pass.Kind == SceneDrawKind::Event) {
        const uint32_t c = pass.Kind == SceneDrawKind::Background ? 0 : 1;
        const auto& slot = state.SceneImages.Slots.FloatSlots[c][pass.Index];
        const auto* pixels = images.AssetPixels(
            {ImageCategorySection(c), uint32_t(slot.Texture)});
        if (!pixels) throw UnsupportedCommand("scene image pixels are missing");
        auto sprite = BuildFloatSceneImage(slot, c, *state.Quake[0]);
        upload(sprite.Texture, *pixels);
        drawSprite(sprite);
        spriteDraws.push_back(sprite);
      } else if (pass.Kind == SceneDrawKind::Characters) {
        for (auto id : SceneCharacterDrawOrder(state)) {
          const auto& actor = state.Characters.at(id);
          const auto* blink = vm->SpriteBlink(id);
          if (!blink) throw UnsupportedCommand("scene actor blink is missing");
          auto poseState = CurrentSceneCharacterPoseState(actor, blink->Phase,
                                                          2, *state.Quake[1]);
          if (!poseState.Visible || !poseState.Color[3]) continue;
          if (actor.Tweens[size_t(CharacterTween::PreviousPose)].Current ||
              actor.PreviousFaceOpacity.Current)
            throw UnsupportedCommand(
                "first-scene actor crossfade is not bound");
          if (!actor.Pose.ResourceSlot)
            throw UnsupportedCommand("scene actor resource slot is missing");
          const auto* pose = poses.Get(*actor.Pose.ResourceSlot);
          if (!pose) throw UnsupportedCommand("scene actor pixels are missing");
          for (size_t i = 0; i < pose->Textures.size(); ++i)
            upload(
                {ProfileTextureSection::CharacterPose, pose->Pose, uint32_t(i)},
                pose->Textures[i]);
          for (const auto& sprite :
               BuildCurrentCharacterPose(*pose, poseState)) {
            drawSprite(sprite);
            spriteDraws.push_back(sprite);
          }
        }
      } else if (pass.Kind == SceneDrawKind::MessageWindow) {
        OrdinaryMessageWindowState windowState;
        windowState.Openness = state.WindowOpenness[0];
        windowState.OpacitySetting = opacity;
        windowState.Quake = *state.Quake[2];
        windowState.SpeakerId = state.SpeakerId;
        windowState.MessageCommand = true;
        windowState.CursorVisible = backend.CursorVisible();
        windowState.CursorOffset = messageCursor.Current;
        if (state.SpeakerId >= 0) uploadMain(uint32_t(state.SpeakerId) + 41);
        for (const auto& draw :
             BuildOrdinaryMessageWindow(visual.Window, windowState)) {
          if (draw.Kind == MessageWindowDrawKind::Sprite) {
            drawSprite(draw.Sprite);
            spriteDraws.push_back(draw.Sprite);
          } else if (draw.Kind == MessageWindowDrawKind::Widget) {
            // The fresh preview maps only advance. Native no-skip/auto-input
            // policy keeps these three conditions false; restored/interactive
            // skip/auto controllers are not bound by this preview.
            widgetInputs.push_back({int32_t(draw.Index), 0, 0, draw.Offset[0],
                                    draw.Offset[1], int32_t(draw.Parameter)});
            for (const auto& sprite : BuildOrdinaryMessageWidget(
                     visual.Window, draw.Index, false, false, false,
                     draw.Offset, uint8_t(draw.Parameter))) {
              drawSprite(sprite);
              spriteDraws.push_back(sprite);
            }
          } else if (draw.Kind == MessageWindowDrawKind::AlphaTest)
            glUniform1i(renderer.AlphaMode, draw.Index);
          else if (draw.Kind == MessageWindowDrawKind::RendererState &&
                   draw.Index)
            throw UnsupportedCommand("scene renderer effect is not bound");
        }
      } else if (pass.Kind == SceneDrawKind::MessageText) {
        // This entry is an explicitly fresh owner with no loaded read history.
        textDraws = BuildOrdinaryMessageTextDraws(
            visual, message->Parsed, message->Reveal, layout, format,
            font.Cached(), uint32_t(utility.Size), font.NominalSize(),
            state.WindowOpenness[0], *state.Quake[2], visual.UnreadColor);
        for (const auto& text : textDraws)
          fontTextures.DrawString(renderer, font, text.Text, text.Position[0],
                                  text.Position[1], text.Size, text.Color);
      } else if (pass.Kind == SceneDrawKind::Fill)
        throw UnsupportedCommand("first-scene fill drawing is not bound");
    }
    GlCheck("first scene composition");
    std::string signature = std::to_string(message->Reveal.Revealed) + ':' +
                            std::to_string(backend.CursorVisible());
    for (const auto& [id, actor] : state.Characters) {
      (void)actor;
      signature += ':' + std::to_string(vm->SpriteBlink(id)->Phase);
    }
    if (capture && (saved == 0 || signature != previousSignature ||
                    now - lastSaved >= 1500 || quit)) {
      const auto file = "scene-" + std::to_string(saved++) + ".bmp";
      Capture(captureDirectory / file, width, height);
      report << "{\"file\":\"" << file << "\",\"now\":" << now
             << ",\"script\":" << state.ScriptFileId
             << ",\"command\":" << state.CurrentCommandIndex << ",\"revealed\":"
             << (message->Reveal.Revealed ? "true" : "false")
             << ",\"frame\":" << frame
             << ",\"completed\":" << (quit ? "true" : "false")
             << ",\"bgm\":" << state.BgmTrack.value_or(-1)
             << ",\"window_opacity_setting\":" << int(opacity)
             << ",\"window_open\":" << state.WindowOpenness[0]
             << ",\"cursor_visible\":"
             << (backend.CursorVisible() ? "true" : "false")
             << ",\"cursor_bits\":["
             << std::bit_cast<uint32_t>(messageCursor.Current) << ','
             << std::bit_cast<uint32_t>(messageCursor.From) << ','
             << std::bit_cast<uint32_t>(messageCursor.To)
             << "],\"cursor_epoch\":" << messageCursor.Epoch
             << ",\"cursor_elapsed\":" << messageCursor.Elapsed
             << ",\"speaker\":" << state.SpeakerId << ",\"read_color_enabled\":"
             << (*state.ReadColorEnabled ? "true" : "false")
             << ",\"random_seed\":" << seed
             << ",\"random_draws\":" << state.Random.Draws()
             << ",\"playback_host_reused\":true,\"fresh_unread\":true,"
                "\"prelude_rendered\":false,\"widget_mode\":\"fresh_idle\","
                "\"unbound_passes\":[\"widget_input\","
                "\"active_auto_rotation\",\"letterbox\",\"animation_"
                "groups\",\"effects\"],\"sprites\":[";
      for (size_t i = 0; i < spriteDraws.size(); ++i) {
        if (i) report << ',';
        WriteUiBlit(report, spriteDraws[i]);
      }
      report << "],\"widget_inputs\":[";
      for (size_t i = 0; i < widgetInputs.size(); ++i) {
        if (i) report << ',';
        report << '[';
        for (size_t j = 0; j < widgetInputs[i].size(); ++j) {
          if (j) report << ',';
          report << widgetInputs[i][j];
        }
        report << ']';
      }
      report << "],\"text\":[";
      for (size_t i = 0; i < textDraws.size(); ++i) {
        if (i) report << ',';
        WriteDescription(report, -1, textDraws[i]);
      }
      report << "],\"actor_inputs\":[";
      bool firstInput = true;
      for (auto id : SceneCharacterDrawOrder(state)) {
        const auto& actor = state.Characters.at(id);
        const auto* blink = vm->SpriteBlink(id);
        auto poseInput = CurrentSceneCharacterPoseState(actor, blink->Phase, 2,
                                                        *state.Quake[1]);
        if (!poseInput.Visible || !poseInput.Color[3]) continue;
        if (!firstInput) report << ',';
        firstInput = false;
        const uint32_t rgba = uint32_t(poseInput.Color[0]) |
                              (uint32_t(poseInput.Color[1]) << 8) |
                              (uint32_t(poseInput.Color[2]) << 16) |
                              (uint32_t(poseInput.Color[3]) << 24);
        report << '[' << actor.Pose.Current << ',' << poseInput.Face << ','
               << poseInput.EyePhase << ',' << poseInput.MouthFrame << ','
               << poseInput.ScalePercent << ',' << poseInput.Position[0] << ','
               << poseInput.Position[1] << ',' << rgba << ','
               << poseInput.Visible << ']';
      }
      report << "],\"blink\":[";
      bool first = true;
      for (const auto& [id, actor] : state.Characters) {
        (void)actor;
        if (!first) report << ',';
        first = false;
        const auto* blink = vm->SpriteBlink(id);
        report << '[' << id << ',' << blink->Wait << ',' << blink->WaitTarget
               << ',' << blink->Phase << ',' << blink->Hold << ']';
      }
      report << "]}\n";
      previousSignature = signature;
      lastSaved = now;
    }
    SdlCheck(SDL_GL_SwapWindow(window.Window), "scene frame swap");
    if (!capture) SDL_Delay(16);
  }
  if (capture) {
    report.close();
    pcm.close();
    events.close();
    if (!quit || firstMessage == UINT64_MAX)
      throw UnsupportedCommand("scene preview budget exhausted");
  }
  return 0;
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
                             std::optional<size_t> itemScene,
                             bool captureNavigation, bool captureDialog,
                             bool captureFlow) {
  bool captureCreated = false;
  try {
    if (itemScene) itemFrames = true;
    if (itemFrames) menuChrome = true;
    const bool capture = !captureDirectory.empty();
    const bool owningFlow = itemScene && (!capture || captureFlow);
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
    std::optional<ProfileDescriptionFormat> descriptionFormat;
    std::optional<DialogEnvironment> dialogEnvironment;
    PlainTextLayout promptLayout;
    DialogContentLayout dialogLayout;
    DialogState dialog;
    bool dialogBound = false;
    if (itemScene) {
      const auto usrdir = resourceDat.parent_path();
      authoredScene = resources.LoadProfileScene(*itemScene);
      catalog = LoadProfileItemCatalog(read(usrdir / "first.dat"));
      font = std::make_unique<PcFontFace>(
          read(usrdir / "DFHSR4.TTC"),
          GsfFont::Decode(read(usrdir / "Font.gsf")));
      utility =
          GameUtilityFont::Decode(read(usrdir / "env/env_game_utility.dat"));
      descriptionFormat =
          ProfileDescriptionFormat::Decode(read(usrdir / "env/env_global.dat"));
      dialogEnvironment =
          DialogEnvironment::Decode(read(usrdir / "env/env_dialog.dat"));
      const auto format = TextFormatEnvironment::Decode(
          read(usrdir / "env/env_string_format.dat"), true);
      const auto metrics = TextLayoutMetrics::Decode(
          read(usrdir / "env/env_global.dat"),
          read(usrdir / "env/env_message_window.dat"));
      VoiceTextReveal prompt;
      prompt.PrepareUtf16(DecodeProfileConfirmationPrompt(read(environmentDat)),
                          format, 0);
      promptLayout = LayoutPlainText(
          prompt.Text, format, font->Cached(), metrics,
          dialogEnvironment->Settings[6], dialogEnvironment->TextScales[1],
          uint32_t(utility->Size), font->NominalSize());
      if (dialogEnvironment->ButtonGroups.size() <= 1)
        throw FormatError("missing profile confirmation buttons");
      dialogLayout = BuildDialogContentLayout(
          *dialogEnvironment, {480, 272}, promptLayout.Height,
          dialogEnvironment->ButtonGroups[1].size(), utility->Size);
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
      const SharedUiAssets shared(
          read(resourceDat.parent_path() / "first.dat"));
      for (const auto entry : {0u, 1u, 3u, 4u, 5u, 9u}) {
        const ProfileTextureKey key{ProfileTextureSection::SharedUi, -1, entry};
        renderer.Upload(key, DecodeSceneImage(shared.Load(entry).Data));
      }
      for (const auto id : {4u, 5u, 6u, 7u, 8u, 9u, 10u, 16u, 19u, 20u}) {
        const ProfileTextureKey key{ProfileTextureSection::Interface, -1, id};
        renderer.Upload(
            key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
      }
      for (const auto& character : authoredScene) {
        if (character.Id < 0) throw FormatError("negative profile portrait ID");
        const ProfileTextureKey key{ProfileTextureSection::Portraits, -1,
                                    uint32_t(character.Id)};
        if (!renderer.Textures.contains(key))
          renderer.Upload(
              key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
        const ProfileTextureKey nameKey{ProfileTextureSection::Interface, -1,
                                        12u + uint32_t(character.Id)};
        if (!renderer.Textures.contains(nameKey))
          renderer.Upload(nameKey,
              DecodeSceneImage(resources.LoadProfileTexture(nameKey).Data));
      }
    }
    ProfileWaveAnimation wave;
    wave.Begin(environment.WavePeriod, 0);
    ProfileArrowMotion arrowMotion;
    if (itemScene)
      arrowMotion.Begin(chromeEnvironment->ArrowTravel,
                        chromeEnvironment->ArrowDuration, 0);
    ProfileArrowMotion navigationMotion;
    if (itemScene)
      navigationMotion.Begin(chromeEnvironment->Navigation.Travel,
                             chromeEnvironment->Navigation.Duration, 0);
    std::vector<uint64_t> times{0, 1147, 8001};
    ProfileSelectionState selection;
    ProfileOwnerState previewOwner;
    std::array<int32_t, 3> previewStatChanges{};
    if (itemFrames) {
      if (itemScene)
        previewOwner = MakeFreshProfileOwner(
            ProfileInitialStats::Decode(read(environmentDat)), catalog);
      else
        // The frame-only viewer has no profile HUD/result consumers.
        previewOwner.Known = true;
      BeginProfileSelection(selection, previewOwner,
                            itemScene ? authoredScene : ProfileScene(1));
      if (capture) {
        selection.Phase = 6;
        times = {0, uint64_t(chromeEnvironment->Carousel) / 2,
                 uint64_t(chromeEnvironment->Carousel)};
        if (captureNavigation) {
          selection.Phase = 5;
          times = {0, 500, 501, 502, 503};
        }
        if (captureDialog)
          times = {0, 1, 76, 151, 152, 153, 154, 303, 304, 305, 306};
      }
    }
    std::unique_ptr<SemanticState> flowOwner;
    std::unique_ptr<ArchiveBgmResources> flowMusic;
    std::unique_ptr<ArchiveSeResources> flowSounds;
    std::unique_ptr<SystemSoundAssets> flowSystemSounds;
    std::unique_ptr<MusicPlayback> flowPlayback;
    std::unique_ptr<PlaybackProfileVoice> flowVoice;
    std::unique_ptr<ArchiveProfileServices> flowServices;
    std::unique_ptr<ProfileMode> flowMode;
    TextFormatEnvironment flowFormat;
    TextLayoutMetrics flowMetrics;
    ModeResultVisualEnvironment flowResultVisual;
    ModeResultEnvironment flowShared;
    ProfileResponseVisualEnvironment flowResponseVisual;
    ProfileCursorEnvironment flowCursorEnvironment;
    MessageCursorEnvironment flowMessageCursor;
    ProfileCursorMotion flowCursor;
    PlainTextLayout flowResultLayout, flowResponseLayout;
    bool resultLaidOut = false, responseLaidOut = false, actorUploaded = false,
         flowDone = false;
    std::ofstream flowPcm;
    size_t flowUiSoundRequests = 0;
    std::vector<std::array<int64_t, 2>> flowSoundHistory;
    uint32_t flowBlinkSeed = 0;
    if (owningFlow) {
      const auto root = resourceDat.parent_path();
      flowOwner = std::make_unique<SemanticState>();
      InitializeFreshAudioOwner(*flowOwner);
      InitializeFreshMessageOwner(*flowOwner);
      InitializeFreshProfileEntry(
          *flowOwner, ProfileInitialStats::Decode(read(environmentDat)),
          catalog);
      flowOwner->OtakuMode = int32_t(*itemScene);
      flowFormat = TextFormatEnvironment::Decode(
          read(root / "env/env_string_format.dat"), true);
      flowMetrics =
          TextLayoutMetrics::Decode(read(root / "env/env_global.dat"),
                                    read(root / "env/env_message_window.dat"));
      flowResultVisual = ModeResultVisualEnvironment::DecodeApproach(
          read(root / "env/env_approach.dat"));
      flowResponseVisual =
          ProfileResponseVisualEnvironment::Decode(read(environmentDat));
      flowCursorEnvironment =
          ProfileCursorEnvironment::Decode(read(environmentDat));
      flowMessageCursor = MessageCursorEnvironment::Decode(
          read(root / "env/env_message_window.dat"));
      flowMusic = std::make_unique<ArchiveBgmResources>(root);
      flowSounds = std::make_unique<ArchiveSeResources>(root);
      flowPlayback = std::make_unique<MusicPlayback>(*flowMusic, capture,
                                                     flowSounds.get());
      flowSystemSounds =
          std::make_unique<SystemSoundAssets>(read(root / "first.dat"));
      // Fresh settings7350: option S60235=8; channel coefficient starts at1.
      flowPlayback->BindUiSounds(*flowSystemSounds, ProfileVoiceGain(8, 1.0f));
      // Fresh settings7350 writes S60236=10; fresh channel coefficient is1.
      flowVoice =
          std::make_unique<PlaybackProfileVoice>(*flowPlayback, 10, 1.0f);
      flowServices = std::make_unique<ArchiveProfileServices>(
          resources,
          ScriptNameIndex::FromFirstArchive(read(root / "first.dat")),
          flowFormat,
          TextTimingEnvironment::Decode(read(root / "env/env_config.dat"),
                                        true),
          flowVoice.get());
      // Standalone fresh viewer: record its host clock seed explicitly.
      // This does not reconstruct an arbitrary native game's RNG state.
      flowBlinkSeed = captureFlow ? 0 : uint32_t(SDL_GetTicks());
      flowOwner->Random.Seed(flowBlinkSeed);
      flowServices->InitializeActorBlink(flowBlinkSeed, flowOwner->Random);
      flowShared = ModeResultEnvironment::DecodeApproach(
          read(root / "env/env_approach.dat"));
      flowMode = std::make_unique<ProfileMode>(
          *flowOwner, *flowServices, *chromeEnvironment, flowShared, 0);
      for (const auto& texture : flowServices->MenuTextures())
        if (!renderer.Textures.contains(texture.Key))
          renderer.Upload(texture.Key, texture.Pixels);
      for (const auto id : {2u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 22u}) {
        const ProfileTextureKey key{ProfileTextureSection::MainUi, -1, id};
        renderer.Upload(
            key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
      }
      if (captureFlow) {
        times.clear();
        for (uint64_t t = 0; t <= 40000; t += 16) times.push_back(t);
        flowPcm.open(captureDirectory / "voice-output.pcm", std::ios::binary);
        if (!flowPcm) throw FormatError("cannot create profile PCM output");
      }
    }
    size_t selected = 0;
    std::ofstream report;
    if (capture) {
      report.open(captureDirectory / "frames.jsonl");
      if (!report) throw FormatError("cannot create profile frame report");
    }
    const auto epoch = SDL_GetTicks();
    uint64_t previousFlowNow = 0, lastFlowSaved = 0;
    std::string previousFlowSignature;
    bool quit = false;
    const auto submitUiSound = [&](int32_t id, uint64_t now) {
      if (!owningFlow) return;
      flowPlayback->Submit({GameEventKind::Sound, id});
      ++flowUiSoundRequests;
      flowSoundHistory.push_back({int64_t(now), id});
    };
    while (!quit) {
      SDL_Event event;
      ProfileSelectionInput input;
      DialogInput modalInput;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
          quit = true;
        if (itemFrames && event.type == SDL_EVENT_KEY_DOWN &&
            !event.key.repeat) {
          if (itemScene && selection.ConfirmationOpen) {
            if (event.key.key == SDLK_LEFT) modalInput.Left = true;
            if (event.key.key == SDLK_RIGHT) modalInput.Right = true;
            if (event.key.key == SDLK_RETURN) modalInput.Accept = true;
            if (event.key.key == SDLK_BACKSPACE) modalInput.Cancel = true;
            continue;
          }
          if (event.key.key == SDLK_UP) input.EventDirection = true;
          if (event.key.key == SDLK_DOWN) input.EventDirection = false;
          if (itemScene && event.key.key == SDLK_LEFT)
            input.CharacterDirection = true;
          if (itemScene && event.key.key == SDLK_RIGHT)
            input.CharacterDirection = false;
          if (itemScene && event.key.key == SDLK_RETURN &&
              (owningFlow || selection.Phase == 5 || selection.Phase == 6))
            input.Accept = true;
          if (itemScene && event.key.key == SDLK_BACKSPACE &&
              selection.Phase == 6)
            input.Cancel = true;
        }
      }
      if (quit) break;
      const uint64_t now = capture ? times[selected] : SDL_GetTicks() - epoch;
      if (owningFlow) {
        const auto delta = now - previousFlowNow;
        flowPlayback->Update(float(delta) / 1000.0f);
        if (captureFlow && delta) {
          std::vector<int16_t> pcm(size_t(delta) * 48 * 2);
          flowPlayback->Render(pcm);
          flowPcm.write(reinterpret_cast<const char*>(pcm.data()),
                        pcm.size() * sizeof(int16_t));
        }
        previousFlowNow = now;
        if (captureFlow) {
          if (flowMode->CurrentStage() == ProfileMode::Stage::Selection &&
              (selection.Phase == 5 || selection.Phase == 6))
            input.Accept = true;
          if (dialog.Visible && dialog.Phase == 3) modalInput.Accept = true;
          if (flowMode->CurrentStage() == ProfileMode::Stage::Result &&
              flowMode->Phase() == 12)
            input.Accept = now % 500 == 0;
          if (flowMode->CurrentStage() == ProfileMode::Stage::Response &&
              flowMode->Phase() == 17)
            input.Accept = flowServices->ResponseText().Revealed &&
                           flowVoice->Stopped() &&
                           flowOwner->Random.Draws() >= 2;
        }
      }
      if (itemFrames) {
        if (captureNavigation) {
          if (selected == 0) input.CharacterDirection = true;
          if (selected == 2) input.Accept = true;
          if (selected == 3) input.Cancel = true;
        } else if (captureDialog) {
          if (selected == 0) input.Accept = true;
          if (selected == 4) modalInput.Left = true;
          if (selected == 5) modalInput.Accept = true;
        } else if (capture && !captureFlow && selected == 0)
          input.EventDirection = true;
        if (itemScene && selection.ConfirmationOpen && dialogBound) {
          UpdateDialog(dialog, modalInput, now);
          for (const auto sound : dialog.UiSounds) submitUiSound(sound, now);
          if (!dialog.Visible) input.Confirmation = dialog.Result == 1;
        }
        if (owningFlow) {
          flowDone = flowMode->Update(*flowOwner, input, now);
          selection = flowMode->Selection();
          previewOwner = flowOwner->ProfileOwner;
          previewStatChanges = {};
          if (flowMode->CurrentStage() == ProfileMode::Stage::Result &&
              flowMode->Result().Stats.Started)
            for (size_t i = 0; i < 3; ++i)
              previewStatChanges[i] =
                  flowMode->Result().Stats.Changes[i].Current;
          const bool cursorReady =
              flowMode->CurrentStage() == ProfileMode::Stage::Result
                  ? flowMode->Result().CursorTickRequested
                  : flowMode->Response().CursorTickRequested;
          flowCursor.Update(flowCursorEnvironment, cursorReady, now);
          for (const auto& request : flowOwner->PendingEvents) {
            if (request.Kind == GameEventKind::StopBgm)
              flowPlayback->Submit(request);
            if (request.Kind == GameEventKind::Sound)
              submitUiSound(request.Id, now);
          }
          flowOwner->PendingEvents.clear();
        } else
          UpdateProfileSelection(selection, *chromeEnvironment, input, now);
        if (itemScene && selection.ConfirmationOpen && !dialogBound) {
          BeginDialog(dialog, dialogLayout,
                      dialogEnvironment->ButtonGroups[1].size(), 1, false, true,
                      now);
          dialogBound = true;
          UpdateDialog(dialog, modalInput, now);
          for (const auto sound : dialog.UiSounds) submitUiSound(sound, now);
        }
        if (!selection.ConfirmationOpen) dialogBound = false;
      }
      if (owningFlow) {
        wave = flowMode->Wave();
        arrowMotion = flowMode->Arrows();
        navigationMotion = flowMode->Navigation();
      } else {
        wave.Update(now);
        if (itemScene) arrowMotion.Update(now);
        if (itemScene) navigationMotion.Update(now);
      }
      int width = 0, height = 0;
      SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
               "frame size");
      auto blits = BuildProfileBackdrop(environment, wave.Current);
      const int32_t phase =
          owningFlow ? flowMode->Phase()
          : itemFrames
              ? selection.Phase
              : (capture ? std::array<int32_t, 3>{5, 6, 12}[selected] : 5);
      const int32_t slide = owningFlow
                                ? flowMode->Result().SecondaryProgress.Current
                            : capture && !captureDialog && selected == 2 ? 137
                                                                         : 0;
      const bool responseVisible = owningFlow && flowMode->Response().Visible;
      if (responseVisible) blits.clear();
      if (menuChrome && !responseVisible) {
        const auto chrome =
            BuildProfileMenuChrome(chromeEnvironment->Chrome, phase, slide);
        blits.insert(blits.end(), chrome.begin(), chrome.end());
      }
      std::vector<UiRenderCommand> commands;
      if (itemFrames && !responseVisible)
        commands =
            BuildProfileItemFramePass(chromeEnvironment->ItemFrames, phase, 960,
                                      544, selection.FrameOffsets);
      renderer.Draw(blits, width, height, commands);
      std::optional<ProfileItemContents> itemContents;
      std::optional<ProfileDescriptionDraws> description;
      std::vector<UiSpriteBlit> arrowDraws;
      std::optional<std::array<UiRenderCommand, 2>> portraitDraws;
      std::optional<ProfileHudDraws> hudDraws;
      std::vector<UiSpriteBlit> navigationDraws;
      std::optional<DialogFrameDraws> dialogFrame;
      std::vector<UiSpriteBlit> dialogDecoration;
      std::vector<UiTextBlit> dialogPrompt;
      std::vector<DialogButtonDraws> dialogButtons;
      if (itemScene && !responseVisible) {
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
        arrowDraws = BuildProfileArrows(chromeEnvironment->Arrows, selection,
                                       arrowMotion);
        for (const auto& arrow : arrowDraws)
          renderer.DrawQuad(arrow, renderer.Textures.at(arrow.Texture), false);
        description = BuildProfileDescription(
            *chromeEnvironment, phase, authoredScene.at(ordinal),
            selection.Selectors.at(ordinal), catalog.at(ordinal).Rows,
            utility->Size, descriptionFormat->Separator);
        for (const auto& line : description->Lines)
          fontTextures.DrawString(renderer, *font, line.Text, line.Position[0],
                                  line.Position[1], line.Size, line.Color);
        hudDraws = BuildProfileHud(chromeEnvironment->Hud,
                                   authoredScene.at(ordinal).Id,
                                   previewOwner.Stats, previewStatChanges);
        renderer.DrawQuad(
            hudDraws->BeforePortrait,
            renderer.Textures.at(hudDraws->BeforePortrait.Texture), false);
        portraitDraws = BuildProfilePortrait(chromeEnvironment->Portrait,
                                             authoredScene, ordinal);
        renderer.DrawQuad(
            (*portraitDraws)[0].Sprite,
            renderer.Textures.at((*portraitDraws)[0].Sprite.Texture), false);
        UiSpriteBlit portraitArea;
        portraitArea.Destination = (*portraitDraws)[1].Rectangle;
        portraitArea.Color = (*portraitDraws)[1].Color;
        renderer.DrawQuad(portraitArea, {0, 1, 1}, true);
        for (const auto& blit : hudDraws->AfterPortrait)
          renderer.DrawQuad(blit,renderer.Textures.at(blit.Texture),false);
        navigationDraws = BuildProfileNavigation(chromeEnvironment->Navigation,
                                                 phase, navigationMotion);
        for (const auto& blit : navigationDraws)
          renderer.DrawQuad(blit, renderer.Textures.at(blit.Texture), false);
        GlCheck("profile item content pass");
        if (dialog.Visible) {
          const auto progress = dialog.Opacity.Current;
          dialogFrame = BuildDialogFrame(
              *dialogEnvironment, {480, 272}, dialogLayout.Width,
              dialogLayout.BodyHeight, progress, {0, 0, 0, 90});
          UiSpriteBlit overlay;
          overlay.Destination = dialogFrame->Overlay.Rectangle;
          overlay.Color = dialogFrame->Overlay.Color;
          renderer.DrawQuad(overlay, {0, 1, 1}, true);
          for (const auto& b : dialogFrame->Tiles)
            renderer.DrawQuad(b, renderer.Textures.at(b.Texture), false);
          dialogDecoration =
              BuildDialogDecoration(*dialogEnvironment, *dialogFrame, progress);
          for (const auto& b : dialogDecoration)
            renderer.DrawQuad(b, renderer.Textures.at(b.Texture), false);
          dialogPrompt = BuildPlainTextDraws(
              promptLayout, dialogLayout.PromptPosition,
              uint32_t(utility->Size), dialogEnvironment->Colors[0],
              uint8_t(progress * 255 / 100));
          for (const auto& t : dialogPrompt)
            fontTextures.DrawString(renderer, *font, t.Text, t.Position[0],
                                    t.Position[1], t.Size, t.Color);
          dialogButtons = BuildDialogButtons(
              *dialogEnvironment, {480, 272}, dialogLayout,
              dialogEnvironment->ButtonGroups[1], dialog.Selected,
              dialog.FocusEnabled, progress, utility->Size, font->Cached(),
              font->NominalSize());
          for (const auto& button : dialogButtons) {
            for (const auto& b : button.Background)
              renderer.DrawQuad(b, renderer.Textures.at(b.Texture), false);
            const auto& t = button.Label;
            fontTextures.DrawString(renderer, *font, t.Text, t.Position[0],
                                    t.Position[1], t.Size, t.Color);
          }
          GlCheck("profile confirmation pass");
        }
      }
      std::vector<UiRenderCommand> flowResultCommands;
      std::vector<UiSpriteBlit> flowActorDraws, flowOverlayDraws;
      std::vector<UiTextBlit> flowTextDraws;
      std::optional<ProfileResponseUiDraws> flowResponseDraws;
      int32_t flowFade = 0;
      if (owningFlow) {
        const auto drawSprite = [&](const UiSpriteBlit& b) {
          renderer.DrawQuad(b, renderer.Textures.at(b.Texture), false);
        };
        const auto drawText = [&](const VoiceTextReveal& text,
                                  PlainTextLayout& layout, bool& laidOut,
                                  int32_t textWidth, float scale,
                                  std::array<int32_t, 2> origin) {
          if (!text.Prepared) return;
          if (!laidOut) {
            layout = LayoutPlainText(
                text.Text, flowFormat, font->Cached(), flowMetrics, textWidth,
                scale, uint32_t(utility->Size), font->NominalSize());
            laidOut = true;
          }
          flowTextDraws = BuildRevealingTextDraws(
              layout, text, flowFormat, origin, uint32_t(utility->Size),
              {0, 0, 0, 255}, 255);
          for (const auto& t : flowTextDraws)
            fontTextures.DrawString(renderer, *font, t.Text, t.Position[0],
                                    t.Position[1], t.Size, t.Color);
        };
        if (responseVisible) {
          const auto& response = flowMode->Response();
          if (const auto* bg = flowServices->BackgroundPixels()) {
            const ProfileTextureKey key{
                ProfileTextureSection::ResponseBackground, response.Background,
                0};
            if (!renderer.Textures.contains(key)) renderer.Upload(key, *bg);
            drawSprite({key,
                        {0, 0, 960, 544},
                        {0, 0, 960, 544},
                        {255, 255, 255, 255}});
          }
          const auto* actor = flowServices->ActorPixels();
          if (!actor)
            throw FormatError("visible profile actor has no prepared pose");
          if (!actorUploaded) {
            for (size_t i = 0; i < actor->Textures.size(); ++i)
              renderer.Upload({ProfileTextureSection::CharacterPose,
                               actor->Pose, uint32_t(i)},
                              actor->Textures[i]);
            actorUploaded = true;
          }
          CurrentCharacterPoseDraw actorState;
          actorState.Face = response.Event.Presentation[1];
          actorState.EyePhase = flowServices->Blink().Phase;
          actorState.Position = {700, 0};
          actorState.MouthFrame = flowServices->Mouth().Frame;
          flowActorDraws = BuildCurrentCharacterPose(*actor, actorState);
          for (const auto& b : flowActorDraws) drawSprite(b);
          flowResponseDraws = BuildProfileResponseUi(
              flowResponseVisual, flowServices->ResponseText().Revealed,
              flowCursor.Channel.Current);
          drawSprite(flowResponseDraws->Window);
          drawText(flowServices->ResponseText(), flowResponseLayout,
                   responseLaidOut, flowResponseVisual.TextWidth,
                   flowResponseVisual.TextScale,
                   flowResponseVisual.TextPosition);
          for (const auto& b : flowResponseDraws->Cursors) drawSprite(b);
        } else if (flowMode->Result().Visible) {
          flowResultCommands = BuildProfileResultLayers(
              flowResultVisual, chromeEnvironment->ResultReflection,
              flowMode->Result(), 960, 544);
          renderer.Execute(flowResultCommands);
          glUniform1i(renderer.AlphaMode, 0);
          glDisable(GL_DEPTH_TEST);
          glDisable(GL_STENCIL_TEST);
          drawText(flowServices->ResultText(), flowResultLayout, resultLaidOut,
                   flowResultVisual.TextWidth, flowResultVisual.TextScale,
                   flowResultVisual.TextPosition);
          flowOverlayDraws = BuildProfileResultOverlays(
              flowResultVisual, flowMessageCursor, flowMode->Result(),
              flowServices->ResultText().Revealed, flowCursor.Channel.Current);
          for (const auto& b : flowOverlayDraws) drawSprite(b);
        }
        flowFade = flowMode->CurrentStage() == ProfileMode::Stage::Selection
                       ? selection.Fade.Current
                   : flowMode->CurrentStage() == ProfileMode::Stage::Result
                       ? flowMode->Result().Fade.Current
                       : flowMode->Response().Fade.Current;
        if (flowFade) {
          UiSpriteBlit fade;
          fade.Destination = {0, 0, 960, 544};
          fade.Color = {255, 255, 255, uint8_t(flowFade)};
          renderer.DrawQuad(fade, {0, 1, 1}, true);
        }
        GlCheck("owning profile result/response composition");
      }
      const auto signature =
          std::to_string(phase) + ":" + std::to_string(dialog.Phase) + ":" +
          std::to_string(dialog.Opacity.Current / 50) + ":" +
          std::to_string(responseLaidOut) + ":" +
          std::to_string(owningFlow && flowServices->ResponseText().Revealed) +
          ":" + std::to_string(owningFlow && flowVoice->Stopped()) + ":" +
          std::to_string(owningFlow ? flowServices->Blink().Phase : 0);
      const bool saveFlowFrame =
          signature != previousFlowSignature ||
          ((phase == 12 || phase == 17) && now - lastFlowSaved >= 2000);
      if (capture && (!captureFlow || saveFlowFrame)) {
        previousFlowSignature = signature;
        lastFlowSaved = now;
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
        if (owningFlow) {
          const auto boolText = [](bool value) {
            return value ? "true" : "false";
          };
          report << ",\"flow\":{\"stage\":" << int(flowMode->CurrentStage())
                 << ",\"phase\":" << phase
                 << ",\"complete\":" << boolText(flowDone)
                 << ",\"target_script\":" << flowMode->TargetScript()
                 << ",\"result_revealed\":"
                 << boolText(flowServices->ResultText().Revealed)
                 << ",\"response_revealed\":"
                 << boolText(flowServices->ResponseText().Revealed)
                 << ",\"voice_stopped\":" << boolText(flowVoice->Stopped())
                 << ",\"voice_gain\":" << flowVoice->Gain()
                 << ",\"white_fade\":" << flowFade << ",\"cursor_bits\":"
                 << std::bit_cast<uint32_t>(flowCursor.Channel.Current)
                 << ",\"mouth_frame\":" << flowServices->Mouth().Frame
                 << ",\"ui_sound_requests\":" << flowUiSoundRequests
                 << ",\"eye_phase\":" << flowServices->Blink().Phase
                 << ",\"blink_seed\":" << flowBlinkSeed
                 << ",\"blink_random_draws\":" << flowOwner->Random.Draws()
                 << ",\"ui_sound_playback\":true,\"ui_sound_gain\":0.8"
                 << ",\"ui_sound_active\":"
                 << boolText(flowPlayback->UiSoundPlaying())
                 << ",\"ui_sound_history\":[";
          for (size_t i = 0; i < flowSoundHistory.size(); ++i) {
            if (i) report << ',';
            report << '[' << flowSoundHistory[i][0] << ','
                   << flowSoundHistory[i][1] << ']';
          }
          report << "],\"automatic_blink\":true,"
                    "\"stats\":[";
          for (size_t i = 0; i < 12; ++i) {
            if (i) report << ',';
            report << flowOwner->ProfileOwner.Stats[i];
          }
          report << "],\"result_samples\":[";
          const auto& r = flowMode->Result();
          const std::array<int32_t, 9> samples{
              r.Primary.Current,          r.HorizontalOffset.Current,
              r.HorizontalAlpha.Current,  r.VerticalOffset.Current,
              r.VerticalAlpha.Current,    r.EntryCompletion.Current,
              r.TextAlpha.Current,        r.ResultProgress.Current,
              r.SecondaryProgress.Current};
          for (size_t i = 0; i < samples.size(); ++i) {
            if (i) report << ',';
            report << samples[i];
          }
          const auto sprites = [&](std::string_view name, const auto& values) {
            report << "],\"" << name << "\":[";
            for (size_t i = 0; i < values.size(); ++i) {
              if (i) report << ',';
              WriteUiBlit(report, values[i]);
            }
          };
          report << "],\"result_commands\":[";
          for (size_t i = 0; i < flowResultCommands.size(); ++i) {
            if (i) report << ',';
            WriteUiCommand(report, flowResultCommands[i]);
          }
          sprites("actor_draws", flowActorDraws);
          sprites("outcome_draws", flowOverlayDraws);
          report << "],\"text_draws\":[";
          for (size_t i = 0; i < flowTextDraws.size(); ++i) {
            if (i) report << ',';
            WriteDescription(report, -1, flowTextDraws[i]);
          }
          report << "],\"response_ui\":[";
          if (flowResponseDraws) {
            WriteUiBlit(report, flowResponseDraws->Window);
            for (const auto& b : flowResponseDraws->Cursors) {
              report << ',';
              WriteUiBlit(report, b);
            }
          }
          report << "]}";
        }
        if (menuChrome)
          report << ",\"phase\":" << phase << ",\"result_slide\":" << slide;
        if (itemFrames && !captureFlow) {
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
        if(description){
          report<<",\"description_draws\":[";
          for(size_t i=0;i<3;++i){if(i)report<<',';WriteDescription(report,description->OwnerRow,description->Lines[i]);}
          report<<']';
        }
        if (itemScene) {
          report << ",\"arrow_active\":[" << selection.ArrowActive[0] << ','
                 << selection.ArrowActive[1] << "],\"arrow_motion\":{\"bits\":["
                 << std::bit_cast<uint32_t>(arrowMotion.Current) << ','
                 << std::bit_cast<uint32_t>(arrowMotion.From) << ','
                 << std::bit_cast<uint32_t>(arrowMotion.To)
                 << "],\"duration\":" << arrowMotion.Duration
                 << ",\"epoch\":" << arrowMotion.Epoch
                 << ",\"elapsed\":" << arrowMotion.Elapsed << ",\"offsets\":["
                 << arrowMotion.Offsets[0] << ',' << arrowMotion.Offsets[1]
                 << "]},\"arrow_draws\":[";
          for (size_t i = 0; i < arrowDraws.size(); ++i) {
            if (i) report << ',';
            WriteUiBlit(report, arrowDraws[i]);
          }
          report << ']';
        }
        if (portraitDraws) {
          report << ",\"portrait_draws\":[";
          WriteUiCommand(report, (*portraitDraws)[0]);
          report << ',';
          WriteUiCommand(report, (*portraitDraws)[1]);
          report << ']';
        }
        if (hudDraws) {
          report << ",\"hud_owner_stats\":[";
          for (size_t i = 0; i < 12; ++i) {
            if (i) report << ',';
            report << previewOwner.Stats[i];
          }
          report << "],\"hud_stat_changes\":[0,0,0],\"hud_owner_source\":"
                    "\"fresh_constructor_profile\",\"hud_draws\":[";
          WriteUiBlit(report, hudDraws->BeforePortrait);
          for (const auto& blit : hudDraws->AfterPortrait) {
            report << ',';
            WriteUiBlit(report, blit);
          }
          report << ']';
        }
        if (itemScene) {
          report << ",\"navigation_motion\":{\"bits\":["
                 << std::bit_cast<uint32_t>(navigationMotion.Current) << ','
                 << std::bit_cast<uint32_t>(navigationMotion.From) << ','
                 << std::bit_cast<uint32_t>(navigationMotion.To)
                 << "],\"duration\":" << navigationMotion.Duration
                 << ",\"epoch\":" << navigationMotion.Epoch
                 << ",\"elapsed\":" << navigationMotion.Elapsed
                 << ",\"offsets\":[" << navigationMotion.Offsets[0] << ','
                 << navigationMotion.Offsets[1] << "]},\"navigation_draws\":[";
          for (size_t i = 0; i < navigationDraws.size(); ++i) {
            if (i) report << ',';
            WriteUiBlit(report, navigationDraws[i]);
          }
          report << ']';
        }
        if(itemScene) {
          report << ",\"dialog\":{\"visible\":"
                 << (dialog.Visible ? "true" : "false")
                 << ",\"phase\":" << dialog.Phase
                 << ",\"pending\":" << dialog.PendingPhase.value_or(-1)
                 << ",\"selected\":" << dialog.Selected
                 << ",\"result\":" << dialog.Result
                 << ",\"focus\":" << (dialog.FocusEnabled ? "true" : "false")
                 << ",\"canceled\":" << (dialog.Canceled ? "true" : "false")
                 << ",\"opacity\":" << dialog.Opacity.Current
                 << "},\"dialog_draws\":[";
          bool first = true;
          const auto sprite = [&](const UiSpriteBlit& b) {
            if (!first) report << ',';
            first = false;
            WriteUiBlit(report, b);
          };
          const auto text = [&](const UiTextBlit& t) {
            if (!first) report << ',';
            first = false;
            WriteDescription(report, -1, t);
          };
          if (dialogFrame) {
            WriteUiCommand(report, dialogFrame->Overlay);
            first = false;
            for (const auto& b : dialogFrame->Tiles) sprite(b);
            for (const auto& b : dialogDecoration) sprite(b);
            for (const auto& t : dialogPrompt) text(t);
            for (const auto& button : dialogButtons) {
              for (const auto& b : button.Background) sprite(b);
              text(button.Label);
            }
          }
          report << ']';
        }
        report << "}\n";
      }
      if (capture && ++selected == times.size()) quit = true;
      if (flowDone) quit = true;
      SdlCheck(SDL_GL_SwapWindow(window.Window), "frame swap");
      if (captureFlow) SDL_Delay(1);
      if (!capture) SDL_Delay(16);
    }
    if (capture) {
      report.close();
      if (!report) throw FormatError("cannot save profile frame report");
    }
    if (captureFlow) {
      flowPcm.close();
      if (!flowPcm) throw FormatError("cannot save profile PCM output");
      if (!flowDone)
        throw FormatError("owning profile capture did not finish within40s");
    }
    if (flowDone && !capture)
      return RunCompletedScene(window, renderer, fontTextures, *font, resources,
                               *flowPlayback, *flowMusic, *flowSounds,
                               std::move(*flowOwner), *flowMode,
                               resourceDat.parent_path());
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
                          const std::filesystem::path& captureDirectory,
                          bool captureNavigation, bool captureDialog,
                          bool captureFlow) {
  return RunProfileBackdropViewer(
      usrdir / "resource.dat", usrdir / "env/env_otaku_mode.dat",
      captureDirectory, true, true, scene, captureNavigation, captureDialog,
      captureFlow);
}
int RunFirstSceneViewer(const std::filesystem::path& usrdir,
                        const std::filesystem::path& captureDirectory) {
  bool created = false;
  try {
    const bool capture = !captureDirectory.empty();
    if (capture) {
      const auto output = std::filesystem::weakly_canonical(captureDirectory);
      const auto input = std::filesystem::weakly_canonical(usrdir);
      for (auto parent = output; !parent.empty();
           parent = parent.parent_path()) {
        if (parent == input || parent == input.parent_path())
          throw FormatError("scene capture overlaps original data");
        if (parent == parent.parent_path()) break;
      }
      if (std::filesystem::exists(output))
        throw FormatError("choose a fresh scene capture directory");
      std::filesystem::create_directories(output);
      created = true;
    }
    GameResources resources(SceneRead(usrdir / "resource.dat"));
    const auto profileRaw = SceneRead(usrdir / "env/env_otaku_mode.dat");
    const auto profile = ProfileEnvironment::Decode(profileRaw);
    const auto shared = ModeResultEnvironment::DecodeApproach(
        SceneRead(usrdir / "env/env_approach.dat"));
    const auto format = TextFormatEnvironment::Decode(
        SceneRead(usrdir / "env/env_string_format.dat"), true);
    const auto timing = TextTimingEnvironment::Decode(
        SceneRead(usrdir / "env/env_config.dat"), true);
    SemanticState owner;
    InitializeFreshAudioOwner(owner);
    InitializeFreshMessageOwner(owner);
    InitializeFreshProfileEntry(
        owner, ProfileInitialStats::Decode(profileRaw),
        LoadProfileItemCatalog(SceneRead(usrdir / "first.dat")));
    ArchiveProfileServices services(
        resources,
        ScriptNameIndex::FromFirstArchive(SceneRead(usrdir / "first.dat")),
        format, timing);
    ProfileMode mode(owner, services, profile, shared);
    ProfileSelectionInput input;
    input.Accept = true;
    input.Confirmation = true;
    uint64_t now = 0;
    for (size_t n = 0;
         n < 2500 && mode.CurrentStage() != ProfileMode::Stage::Complete; ++n) {
      mode.Update(owner, input, now += 16);
      owner.PendingEvents.clear();
    }
    if (mode.CurrentStage() != ProfileMode::Stage::Complete)
      throw FormatError("headless profile entry did not complete");
    ViewerWindow window(capture, true);
    SdlCheck(SDL_SetWindowSize(window.Window, 960, 544), "scene viewport size");
    UiBlitRenderer renderer;
    FontTextures fontTextures;
    PcFontFace font(SceneRead(usrdir / "DFHSR4.TTC"),
                    GsfFont::Decode(SceneRead(usrdir / "Font.gsf")));
    ArchiveBgmResources music(usrdir);
    ArchiveSeResources sounds(usrdir);
    MusicPlayback playback(music, capture, &sounds);
    return RunCompletedScene(window, renderer, fontTextures, font, resources,
                             playback, music, sounds, std::move(owner), mode,
                             usrdir, captureDirectory);
  } catch (const std::exception& error) {
    if (created) {
      std::ofstream out(captureDirectory / "error.txt");
      out << std::string(error.what()).substr(0, 2048) << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano scene: %s",
                 error.what());
    return 1;
  }
}
}  // namespace Impacto::Saekano
