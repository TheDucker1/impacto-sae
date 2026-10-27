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
#include "album.h"
#include "scenemessage.h"
#include "messagedraw.h"
#include "scenequeue.h"
#include "widgetinput.h"
#include "skipauto.h"
#include "mouthselection.h"
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
#include <cmath>

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
  out << ']';
  if (blit.Rotation)
    out << ",\"rotation\":{\"radians_bits\":"
        << std::bit_cast<uint32_t>(blit.Rotation->Radians)
        << ",\"pivot\":[" << blit.Rotation->Pivot[0] << ','
        << blit.Rotation->Pivot[1] << "]}";
  out << '}';
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
  GLint FilterMode = -1, FilterValue = -1, Glyph = -1;
  GLint ColoredRule = -1, RuleValue = -1;
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
  explicit UiBlitRenderer(const char* referenceFragment = nullptr) {
    try {
      Vertex = Compile(GL_VERTEX_SHADER, R"GLSL(#version 330
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
out vec2 texturePosition;
void main(){texturePosition=uv;gl_Position=vec4(position.x/480.0-1.0,1.0-position.y/272.0,0.0,1.0);}
)GLSL");
      Fragment = Compile(GL_FRAGMENT_SHADER, referenceFragment ? referenceFragment : R"GLSL(#version 330
#extension GL_ARB_shading_language_packing : require
#extension GL_ARB_gpu_shader5 : require
in vec2 texturePosition;
uniform sampler2D image;
uniform vec4 tint;
uniform bool solid;
uniform int alphaMode;
uniform int filterMode;
uniform float filterValue;
uniform bool glyph;
uniform bool coloredRule;
uniform float ruleValue;
out vec4 outputColor;
void main(){
  outputColor=solid?tint:texture(image,texturePosition)*tint;
  // Native rectangles and glyphs use independent fragment programs.
  if(coloredRule){
    vec4 result=vec4(tint.rgb,clamp(texture(image,texturePosition).a+ruleValue,0.0,1.0));
    outputColor=vec4(unpackHalf2x16(packHalf2x16(result.xy)),unpackHalf2x16(packHalf2x16(result.zw)));
  }else if(filterMode==1 && !solid && !glyph){
    float value=clamp(filterValue,0.0,1.0);
    float luminance=dot(vec3(0.298911988735199,0.5866109728813171,0.11447799950838089),outputColor.rgb);
    float weighted=value*luminance;
    vec3 base=outputColor.rgb*(1.0-value);
    vec3 rgb=fma(vec3(weighted),vec3(0.9411764740943909,0.7843137383460999,0.5686274766921997),base);
    vec4 result=tint*vec4(rgb,outputColor.a);
    outputColor=vec4(unpackHalf2x16(packHalf2x16(result.xy)),unpackHalf2x16(packHalf2x16(result.zw)));
  }
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
      FilterMode = glGetUniformLocation(Program, "filterMode");
      FilterValue = glGetUniformLocation(Program, "filterValue");
      Glyph = glGetUniformLocation(Program, "glyph");
      ColoredRule = glGetUniformLocation(Program, "coloredRule");
      RuleValue = glGetUniformLocation(Program, "ruleValue");
      GlCheck("UI renderer initialization");
    } catch (...) {
      Release();
      throw;
    }
  }
  void SetFilter(int32_t mode, float value) {
    if (mode != 0 && mode != 1)
      throw UnsupportedCommand("scene screen filter mode is not bound");
    if (!std::isfinite(value)) throw FormatError("nonfinite scene filter intensity");
    glUniform1i(FilterMode, mode);
    glUniform1f(FilterValue, std::clamp(value, 0.0f, 1.0f));
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
    SetFilter(0, 0.0f);
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
                bool solid, bool glyph = false,
                std::optional<float> coloredRule = {}) {
    if (coloredRule && !std::isfinite(*coloredRule))
      throw FormatError("nonfinite rule shader value");
    glUniform1i(Solid, solid ? 1 : 0);
    glUniform1i(Glyph, glyph ? 1 : 0);
    glUniform1i(ColoredRule, coloredRule.has_value() ? 1 : 0);
    glUniform1f(RuleValue, coloredRule.value_or(0.0f));
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
        renderer.DrawQuad(blit, Get(bitmap, id, *pixels), false, true);
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
// An ordinary-dialogue frontend. Unbound auxiliary/effect passes are
// reported explicitly; this is not yet the complete scenario renderer.
int RunCompletedScene(ViewerWindow& window, UiBlitRenderer& renderer,
                      FontTextures& fontTextures, PcFontFace& font,
                      GameResources& resources, MusicPlayback& playback,
                      ArchiveBgmResources& music, ArchiveSeResources& sounds,
                      SemanticState&& owner, const ProfileMode& completed,
                      const std::filesystem::path& root,
                      const std::filesystem::path& captureDirectory = {},
                      bool captureAuto = false, bool captureButtonAuto = false,
                      bool captureVoiced = false,
                      std::optional<size_t> captureUntilMessage = {},
                      std::optional<size_t> captureFromCommand = {}) {
  const bool capture = !captureDirectory.empty();
  if (!owner.WindowOpacity || !owner.ReadColorEnabled || !owner.TextSpeed ||
      !owner.AutoSpeed)
    throw UnsupportedCommand("scene owner display settings are missing");
  const auto opacity = *owner.WindowOpacity;
  const auto format = TextFormatEnvironment::Decode(
      SceneRead(root / "env/env_string_format.dat"), true);
  const auto timing = TextTimingEnvironment::Decode(
      SceneRead(root / "env/env_config.dat"), true, true);
  const auto visual = OrdinaryTextEnvironment::Decode(
      SceneRead(root / "env/env_message_window.dat"));
  const auto inputEnvironment = WidgetInputEnvironment::DecodeUtility(
      SceneRead(root / "env/env_game_utility.dat"));
  OrdinaryWidgetInput sceneInput(visual.Window, inputEnvironment.HoldFrames);
  const auto skipAutoEnvironment = SkipAutoEnvironment::Decode(
      SceneRead(root / "env/env_global.dat"));
  SkipAutoState skipAuto;
  OrdinaryAutoRotation autoRotation;
  autoRotation.Begin(visual.Window.AutoRotationDuration, 0);
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
  SceneMessageBackend backend(format, timing, *owner.TextSpeed, owner.AutoSpeed);
  backend.SceneImages = &images;
  backend.CharacterPoses = &poses;
  backend.MusicResources = &music;
  backend.SeArchives = &sounds;
  auto vm = VirtualMachine::FromCompletedProfile(resources, backend,
                                                 std::move(owner), completed);
  const auto captureScript = vm->State().ScriptFileId;
  if (captureFromCommand && (!captureUntilMessage || *captureFromCommand > *captureUntilMessage))
    throw FormatError("capture range requires a start before its target message");
  if (captureUntilMessage) {
    const ScriptObject script(resources.Load(captureScript));
    if (*captureUntilMessage >= script.Records().size() ||
        script.Records()[*captureUntilMessage].Opcode != 0x64)
      throw FormatError("capture target is not a message in the entry script");
    if (!capture || !captureAuto)
      throw FormatError("capture target requires automatic capture mode");
  }
  bool targetCompleted = false;
  bool captureActive = !captureFromCommand;
  std::optional<uint64_t> pcmEpoch;
  auto mouthControl = MakeFreshOrdinaryMouthControl();
  std::map<uint16_t, ProfileMouthPlayback> mouths;
  SpriteMessageMouth spriteMouth(vm->State(), mouthControl, mouths);
  const auto voiceSettings = MakeFreshOrdinaryVoiceSettings();
  PlaybackOrdinaryVoice voices(playback, root, voiceSettings.Volume, 1.0f);
  std::unique_ptr<SceneMessageVoiceOwner> voiceOwner;
  int32_t voiceScript = -1;
  const auto bindScriptVoices = [&] {
    if (voiceScript == vm->State().ScriptFileId) return;
    backend.ResetMessages();
    mouths.clear();
    mouthControl = MakeFreshOrdinaryMouthControl();
    voiceOwner = std::make_unique<SceneMessageVoiceOwner>(SceneMessageVoiceOwner{
        voices, spriteMouth, MakeFreshOrdinaryVoiceQueue(),
        resources.LoadScriptVoiceEnvelopes(vm->State().ScriptFileId),
        LoadOrdinaryVoiceRangesFromFirst(SceneRead(root / "first.dat")), voiceSettings});
    for (const auto& envelope : voiceOwner->Envelopes)
      voiceOwner->Queue.Catalog.push_back(int16_t(envelope.VoiceId));
    backend.BindVoiceOwner(*voiceOwner);
    voiceScript = vm->State().ScriptFileId;
  };
  bindScriptVoices();
  const auto syncMouths = [&] {
    // This entry replaces the manager with fresh sprite runtimes. Unknown
    // restored animation state is not filled by this frontend.
    for (const auto& [id, actor] : vm->State().Characters) {
      if (!actor.RuntimeKnown) throw UnsupportedCommand("scene mouth actor runtime is unknown");
      mouths.try_emplace(id);
    }
    std::erase_if(mouths, [&](const auto& item) { return !vm->State().Characters.contains(item.first); });
  };
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
  size_t layoutCommand = SIZE_MAX;
  bool laidOut = false, quit = false;
  bool pointerDown = false;
  bool autoKeyHeld = false;
  std::array<bool, 2> skipKeysHeld{};
  uint32_t previousPolicyFlags = UINT32_MAX;
  uint32_t captureAutoStage = 0;
  std::array<float, 2> pointerPosition{};
  uint32_t capturePointerStage = 0;
  std::string previousPointer;
  uint64_t previous = 0, firstMessage = UINT64_MAX, lastSaved = 0;
  const auto epoch = SDL_GetTicks();
  std::string previousSignature;
  size_t saved = 0;
  std::map<uint16_t, bool> actorEligibility;
  const uint32_t frameBudget = captureFromCommand ? 20000 : 2000;
  for (uint32_t frame = 0; !quit && (!capture || frame < frameBudget); ++frame) {
    const uint64_t now =
        capture ? uint64_t(frame) * 16 : SDL_GetTicks() - epoch;
    messageCursor.Update(now);
    CommandInput input;
    SkipAutoInput policyInput;
    SDL_Event event;
    if (capture && !captureAuto && firstMessage != UINT64_MAX &&
        capturePointerStage < (captureButtonAuto ? 2u : 4u)) {
      constexpr std::array<uint64_t, 4> Delays{96, 112, 512, 528};
      if (now - firstMessage >= Delays[capturePointerStage]) {
        SDL_Event synthetic{};
        synthetic.type = (capturePointerStage % 2 == 0)
                             ? SDL_EVENT_MOUSE_BUTTON_DOWN
                             : SDL_EVENT_MOUSE_BUTTON_UP;
        synthetic.button.windowID = SDL_GetWindowID(window.Window);
        synthetic.button.button = SDL_BUTTON_LEFT;
        synthetic.button.down = capturePointerStage % 2 == 0;
        synthetic.button.x = 400;
        synthetic.button.y = 440;
        if (captureButtonAuto) {
          const auto button = ResolveUiSprite(
              {ProfileTextureSection::MainUi, -1, 3}, visual.Window.Auxiliary[5]);
          synthetic.button.x = float(button.Destination[0] + button.Destination[2]/2);
          synthetic.button.y = float(button.Destination[1] + button.Destination[3]/2);
        }
        SdlCheck(SDL_PushEvent(&synthetic), "capture pointer event");
        ++capturePointerStage;
      }
    }
    if (capture && captureAuto && firstMessage != UINT64_MAX && captureAutoStage < 2) {
      const uint64_t delay = captureAutoStage == 0 ? 96 : 1296;
      if (now - firstMessage >= delay) {
        SDL_Event synthetic{};
        synthetic.type = captureAutoStage == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        synthetic.key.windowID = SDL_GetWindowID(window.Window);
        synthetic.key.key = SDLK_A;
        synthetic.key.down = captureAutoStage == 0;
        SdlCheck(SDL_PushEvent(&synthetic), "capture auto-key event");
        ++captureAutoStage;
      }
    }
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT ||
          (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
        quit = true;
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
          event.key.key == SDLK_RETURN)
        input.Advance = true;
      if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
          event.key.key == SDLK_A) {
        autoKeyHeld = event.type == SDL_EVENT_KEY_DOWN;
        policyInput.AutoPressed |= autoKeyHeld && !event.key.repeat;
      }
      if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        if (event.key.key == SDLK_LCTRL || event.key.key == SDLK_RCTRL)
          skipKeysHeld[event.key.key == SDLK_RCTRL] = event.type == SDL_EVENT_KEY_DOWN;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB &&
            !event.key.repeat)
          policyInput.SkipPressed = true;
      }
      if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
          event.button.button == SDL_BUTTON_LEFT) {
        pointerDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        pointerPosition = {event.button.x, event.button.y};
      }
      if (event.type == SDL_EVENT_MOUSE_MOTION)
        pointerPosition = {event.motion.x, event.motion.y};
      if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        pointerDown = false;
        autoKeyHeld = false;
        policyInput.AutoPressed = false;
        skipKeysHeld = {};
        policyInput.SkipPressed = false;
        sceneInput.CancelPointer();
      }
    }
    if (quit) break;
    // Dedicated fresh scene: no hidden-message mode or blocking overlay, and
    // no restored read history. History eligibility is an explicit false input
    // for this fresh-unread preview; a loaded-history owner must supply its lookup.
    // Policy consumes prior widget samples before this frame's updates.
    policyInput.AutoHeld = autoKeyHeld;
    policyInput.SkipHeld = skipKeysHeld[0] || skipKeysHeld[1];
    policyInput.SkipButtonReleased = sceneInput.Query(
        1, WidgetQuery::Released, skipAuto.SkipIndicators[0], skipAuto.SkipIndicators[1]);
    policyInput.AutoButtonReleased = sceneInput.Query(
        2, WidgetQuery::Released, skipAuto.AutoIndicator);
    policyInput.PrimaryFullHold = sceneInput.Query(0, WidgetQuery::FullHold);
    policyInput.PrimaryFlick = sceneInput.Query(0, WidgetQuery::Flick);
    policyInput.PrimaryVelocityX = sceneInput.Components()[0].Velocity[0];
    const uint32_t policyFlags =
        (policyInput.SkipHeld ? 8u : 0u) | (policyInput.SkipPressed ? 16u : 0u) |
        (policyInput.SkipButtonReleased ? 32u : 0u) | (policyInput.PrimaryFlick ? 64u : 0u) |
        (policyInput.AutoPressed ? 128u : 0u) | (policyInput.AutoHeld ? 256u : 0u) |
        (policyInput.AutoButtonReleased ? 512u : 0u) | (policyInput.PrimaryFullHold ? 1024u : 0u);
    const bool wasAuto = skipAuto.Auto;
    vm->UpdateSkipAuto(skipAuto, policyInput, skipAutoEnvironment.AutoKeyDuration, now);
    backend.AutoAdvance = skipAuto.Auto;
    input.Route = vm->State().SkipPhase ? UpdateRoute::Skipping : UpdateRoute::Normal;
    if (capture && policyFlags != previousPolicyFlags) {
      events << "{\"kind\":\"widget_policy\",\"frame\":" << frame
             << ",\"now\":" << now << ",\"flags\":" << policyFlags
             << ",\"velocity_x_bits\":" << std::bit_cast<uint32_t>(policyInput.PrimaryVelocityX)
             << ",\"mode\":" << vm->State().SkipMode << ",\"phase\":" << vm->State().SkipPhase
             << ",\"auto\":" << skipAuto.Auto << "}\n";
      previousPolicyFlags = policyFlags;
    }
    if (capture && (policyInput.AutoPressed || wasAuto != skipAuto.Auto))
      events << "{\"kind\":\"auto_policy\",\"frame\":" << frame
             << ",\"now\":" << now << ",\"pressed\":" << policyInput.AutoPressed
             << ",\"held\":" << autoKeyHeld << ",\"auto\":" << skipAuto.Auto << "}\n";
    WidgetInputHistory::Snapshot pointerSnapshot;
    if (pointerDown) {
      int logicalWidth = 0, logicalHeight = 0;
      SdlCheck(SDL_GetWindowSize(window.Window, &logicalWidth, &logicalHeight),
               "pointer viewport size");
      const float x = pointerPosition[0] * 960.0f / float(logicalWidth),
                  y = pointerPosition[1] * 544.0f / float(logicalHeight);
      if (x >= INT16_MIN && x <= INT16_MAX && y >= INT16_MIN && y <= INT16_MAX)
        pointerSnapshot[0] = WidgetTouch{0, {int16_t(x), int16_t(y)}};
    }
    sceneInput.BeginFrame(pointerSnapshot);
    const bool pointerAdvance = sceneInput.UpdatePrimary();
    input.Advance = input.Advance || pointerAdvance;
    if (capture) {
      const auto& point = pointerSnapshot[0];
      const auto signature = point ? std::to_string(point->Position[0]) + ',' +
                                         std::to_string(point->Position[1])
                                   : "none";
      if (signature != previousPointer) {
        events << "{\"kind\":\"pointer\",\"frame\":" << frame
               << ",\"present\":" << (point ? "true" : "false");
        if (point)
          events << ",\"position\":[" << point->Position[0] << ','
                 << point->Position[1] << ']';
        events << ",\"synthetic\":true}\n";
        previousPointer = signature;
      }
      if (pointerAdvance)
        events << "{\"kind\":\"pointer_advance\",\"frame\":" << frame << "}\n";
    }
    playback.Update(float(now - previous) / 1000.0f);
    if (capture && now > previous) {
      std::vector<int16_t> samples(size_t(now - previous) * 96);
      playback.Render(samples);
      if (captureActive) {
        if (!pcmEpoch) pcmEpoch = previous;
        pcm.write(reinterpret_cast<const char*>(samples.data()),
                  samples.size() * sizeof(int16_t));
      }
    }
    previous = now;
    for (size_t work = 0; work < 256; ++work) {
      bindScriptVoices();
      syncMouths();
      const auto step = vm->Step(now, input);
      if (captureFromCommand && step.Script == captureScript &&
          size_t(step.Command) >= *captureFromCommand) captureActive = true;
      input.Advance = false;
      syncMouths();
      if (capture && (step.Boundary == VmBoundary::CommandStarted ||
                      step.Boundary == VmBoundary::CommandCompletedBeforeDispatch))
        events << "{\"kind\":\"command\",\"frame\":" << frame << ",\"now\":" << now
               << ",\"script\":" << step.Script << ",\"command\":" << step.Command
               << ",\"opcode\":" << step.Opcode << ",\"boundary\":" << int(step.Boundary) << "}\n";
      if (step.Opcode == 0x64 && step.Boundary == VmBoundary::CommandStarted) {
        firstMessage = now;
        laidOut = false;
        capturePointerStage = 0;
        break;
      }
      if (step.Opcode == 0x64 &&
          step.Boundary == VmBoundary::CommandCompletedBeforeDispatch && capture &&
          (captureUntilMessage
               ? step.Script == captureScript && size_t(step.Command) == *captureUntilMessage
               : (!captureVoiced || step.Command >= 67))) {
        targetCompleted = true;
        quit = true;
        break;
      }
      if (step.Boundary == VmBoundary::WaitingInput ||
          step.Boundary == VmBoundary::WaitingBackend)
        break;
      if (step.Boundary == VmBoundary::Ended) {
        quit = true;
        break;
      }
      if (work == 255)
        throw UnsupportedCommand("scene per-frame command budget exceeded");
    }
    for (const auto& request : vm->TakeEvents()) {
      if (!backend.ConsumeVoiceEvent(request)) playback.Submit(request);
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
    for (const auto& [id, actor] : vm->State().Characters)
      if (actor.Pose.Ready && actor.Tweens[size_t(CharacterTween::Opacity)].Current != 0)
        mouths.at(id).Update(now);
    backend.UpdateMessage(now);
    const auto& state = vm->State();
    if (!state.ScreenFilter.Mode || !state.ScreenFilter.Intensity)
      throw UnsupportedCommand("scene screen filter state is unknown");
    // Primary updates before command work; button widgets update every variant
    // afterwards, using the new frame's slide/quake offset.
    if (!state.Quake[2])
      throw UnsupportedCommand("widget quake inputs are missing");
    const int32_t slide =
        std::bit_cast<int32_t>(uint32_t(visual.Window.WindowTravel) *
                               (100u - state.WindowOpenness[0])) /
        100;
    sceneInput.UpdateButtons(
        {(*state.Quake[2])[0],
         std::bit_cast<int32_t>(uint32_t(slide) +
                                uint32_t((*state.Quake[2])[1]))});
    autoRotation.Update(now);
    int width = 0, height = 0;
    SdlCheck(SDL_GetWindowSizeInPixels(window.Window, &width, &height),
             "scene frame size");
    renderer.Draw({}, width, height);
    renderer.SetFilter(*state.ScreenFilter.Mode, *state.ScreenFilter.Intensity);
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
    if (state.ScreenFade != 0)
      throw UnsupportedCommand(
          "first-scene active screen-fade rendering is not bound");
    if (!state.Quake[0] || !state.Quake[1] || !state.Quake[2])
      throw UnsupportedCommand("scene quake inputs are missing");
    if (!laidOut || layoutCommand != message->Command) {
      layout =
          LayoutPlainText(message->Reveal.Text, format, font.Cached(), metrics,
                          visual.Window.TextWidth, visual.BodyScale,
                          uint32_t(utility.Size), font.NominalSize());
      laidOut = true;
      layoutCommand = message->Command;
    }
    SceneQueueInputs gates;
    gates.EffectPlacement = state.RuleWipe.Parameter;
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
          const auto draws = BuildSceneCharacterPose(
              actor, poses, blink->Phase, mouths.at(id).Frame, *state.Quake[1]);
          for (const auto& sprite : draws) {
            // Both retained and current resource slots can contribute a pass.
            const PreparedCharacterPose* pixels = nullptr;
            for (const auto slot : {actor.Pose.PreviousResourceSlot,
                                    actor.Pose.ResourceSlot}) {
              if (!slot) continue;
              const auto* candidate = poses.Get(*slot);
              if (candidate && candidate->Pose == sprite.Texture.Character) pixels = candidate;
            }
            if (!pixels || sprite.Texture.Entry >= pixels->Textures.size())
              throw UnsupportedCommand("scene actor draw texture is missing");
            upload(sprite.Texture, pixels->Textures[sprite.Texture.Entry]);
            drawSprite(sprite);
            spriteDraws.push_back(sprite);
          }
        }
      } else if (pass.Kind == SceneDrawKind::Effect) {
        const auto effect = BuildRuleWipeDraw(state.RuleWipe);
        if (effect.Kind == RuleWipeDrawKind::MaskedImage || effect.Kind == RuleWipeDrawKind::Image)
          throw UnsupportedCommand("rule-wipe image-background shader/state is not bound");
        if (effect.Kind != RuleWipeDrawKind::None) {
          UiSpriteBlit sprite;
          sprite.Destination = effect.Destination; sprite.Source = effect.Source;
          sprite.Color = effect.Color;
          if (effect.Kind == RuleWipeDrawKind::Solid) {
            renderer.DrawQuad(sprite, {0, 1, 1}, true);
          } else {
            const auto* pixels = images.Pixels(ImageSlot::Rule);
            if (!pixels || images.Key(ImageSlot::Rule) != effect.Rule)
              throw UnsupportedCommand("rule-wipe mask pixels do not match active resource");
            sprite.Texture = {ProfileTextureSection::SceneImage, int32_t(ImageSection::Rule), effect.Rule->Id};
            upload(sprite.Texture, *pixels);
            renderer.DrawQuad(sprite, renderer.Textures.at(sprite.Texture), false, false, effect.Value);
          }
          spriteDraws.push_back(sprite);
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
        windowState.RendererMode = uint32_t(*state.ScreenFilter.Mode);
        windowState.RendererParameter = std::bit_cast<uint32_t>(*state.ScreenFilter.Intensity);
        if (state.SpeakerId >= 0) uploadMain(uint32_t(state.SpeakerId) + 41);
        for (const auto& draw :
             BuildOrdinaryMessageWindow(visual.Window, windowState)) {
          if (draw.Kind == MessageWindowDrawKind::Sprite) {
            drawSprite(draw.Sprite);
            spriteDraws.push_back(draw.Sprite);
          } else if (draw.Kind == MessageWindowDrawKind::Widget) {
            widgetInputs.push_back({int32_t(draw.Index), int32_t(skipAuto.SkipIndicators[0]) |
                                        (int32_t(skipAuto.SkipIndicators[1]) << 1),
                                    int32_t(skipAuto.AutoIndicator), draw.Offset[0],
                                    draw.Offset[1], int32_t(draw.Parameter)});
            for (const auto& sprite : BuildOrdinaryMessageWidget(
                     visual.Window, draw.Index, skipAuto.SkipIndicators[0], skipAuto.SkipIndicators[1], skipAuto.AutoIndicator,
                     draw.Offset, uint8_t(draw.Parameter), autoRotation.Channel.Current)) {
              drawSprite(sprite);
              spriteDraws.push_back(sprite);
            }
          } else if (draw.Kind == MessageWindowDrawKind::AlphaTest)
            glUniform1i(renderer.AlphaMode, draw.Index);
          else if (draw.Kind == MessageWindowDrawKind::RendererState)
            renderer.SetFilter(int32_t(draw.Index), std::bit_cast<float>(draw.Parameter));
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
    renderer.SetFilter(0, 0.0f);  // Native resets after queued callbacks, before later HUD.
    GlCheck("first scene composition");
    std::string signature = std::to_string(message->Reveal.Revealed) + ':' +
                            std::to_string(backend.CursorVisible()) + ':' +
                            std::to_string(pointerDown) + ':' +
                            std::to_string(skipAuto.Auto) + ':' + std::to_string(autoKeyHeld);
    if (captureVoiced) signature = std::to_string(message->Command) + ':' +
        std::to_string(message->Reveal.Revealed) + ':' + std::to_string(message->VoiceGate.Phase);
    signature += ':' + std::to_string(state.RuleWipe.Active) + ':' +
        std::to_string(state.RuleWipe.Auxiliary) + ':' + std::to_string(state.RuleWipe.Progress.value_or(0) / 25);
    for (const auto& [id, actor] : state.Characters) {
      if (!captureVoiced) signature += ':' + std::to_string(vm->SpriteBlink(id)->Phase);
      if (captureUntilMessage) signature += ':' + std::to_string(actor.Pose.Current) + ':' +
          std::to_string(actor.Tweens[size_t(CharacterTween::PreviousPose)].Current / 64);
    }
    if (capture && captureActive && (saved == 0 || signature != previousSignature ||
                    now - lastSaved >= 1500 || quit)) {
      const auto file = "scene-" + std::to_string(saved++) + ".bmp";
      Capture(captureDirectory / file, width, height);
      report << "{\"file\":\"" << file << "\",\"now\":" << now
             << ",\"script\":" << state.ScriptFileId
             << ",\"command\":" << state.CurrentCommandIndex << ",\"revealed\":"
             << (message->Reveal.Revealed ? "true" : "false")
             << ",\"frame\":" << frame
             << ",\"message_command\":" << message->Command
             << ",\"pcm_epoch_ms\":";
      if (pcmEpoch) report << *pcmEpoch; else report << "null";
      report
             << ",\"completed\":" << (quit ? "true" : "false")
             << ",\"bgm\":" << state.BgmTrack.value_or(-1)
             << ",\"screen_fade\":" << state.ScreenFade.value_or(-1)
             << ",\"screen_filter\":[" << *state.ScreenFilter.Mode << ',' << *state.ScreenFilter.Intensity << ']'
             << ",\"rule_wipe\":[" << state.RuleWipe.Active << ',' << state.RuleWipe.Auxiliary
             << ',' << state.RuleWipe.Progress.value_or(0) << ',' << state.RuleWipe.CachedRule
             << ',' << state.RuleWipe.Parameter << ',' << state.RuleWipe.BackgroundKind << ']'
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
                "\"primary_pointer_bound\":true,\"synthetic_pointer_capture\":"
             << (!captureAuto ? "true," : "false,")
             << "\"prelude_rendered\":false,\"auto_key_bound\":true,\"widget_mode\":\"fresh_controls\","
                "\"unbound_passes\":[\"restored_read_history\",\"widget_long_hold_"
                "visual\","
                "\"letterbox\",\"animation_"
                "groups\",\"effects\"],\"auto_state\":[" << skipAuto.Auto << ','
             << autoKeyHeld << ',' << autoRotation.Channel.Current << ','
             << autoRotation.Channel.Epoch << ',' << autoRotation.Channel.Sample
             << "],\"message_epoch\":" << message->Epoch
             << ",\"message_source_units\":" << message->SourceUnits
             << ",\"capture_voiced\":" << (captureVoiced ? "true" : "false")
             << ",\"voice_id\":" << message->VoiceId << ",\"voice_phase\":" << message->VoiceGate.Phase
             << ",\"voice_slot\":" << message->VoiceGate.Slot << ",\"voice_position_ms\":" << message->AudioPosition
             << ",\"skip_state\":[" << state.SkipMode << ',' << state.SkipPhase << ']'
             << ",\"history_gate_supplied\":false"
             << ",\"capture_auto\":" << captureAuto
             << ",\"capture_auto_button\":" << captureButtonAuto << ",\"sprites\":[";
      for (size_t i = 0; i < spriteDraws.size(); ++i) {
        if (i) report << ',';
        WriteUiBlit(report, spriteDraws[i]);
      }
      report << "],\"input_ring\":[" << sceneInput.Input().Count << ','
             << sceneInput.Input().Oldest << ',' << sceneInput.Input().Next
             << "],\"input_offset\":[" << (*state.Quake[2])[0] << ','
             << std::bit_cast<int32_t>(uint32_t(slide) +
                                       uint32_t((*state.Quake[2])[1]))
             << "],\"input_components\":[";
      for (size_t i = 0; i < sceneInput.Components().size(); ++i) {
        if (i) report << ',';
        const auto& component = sceneInput.Components()[i];
        report << "{\"flags\":" << component.Flags
               << ",\"tracked\":" << component.TrackedId
               << ",\"index\":" << component.HistoryIndex
               << ",\"double\":" << component.DoublePressCounter
               << ",\"gesture\":" << component.GestureCounter
               << ",\"secondary\":" << component.SecondaryGestureCounter
               << ",\"toggle\":" << component.Toggle << ",\"delta\":["
               << component.Delta[0] << ',' << component.Delta[1]
               << "],\"velocity_bits\":["
               << std::bit_cast<uint32_t>(component.Velocity[0]) << ','
               << std::bit_cast<uint32_t>(component.Velocity[1])
               << "],\"absolute_bits\":";
        if (component.AbsoluteVelocity)
          report << '['
                 << std::bit_cast<uint32_t>((*component.AbsoluteVelocity)[0])
                 << ','
                 << std::bit_cast<uint32_t>((*component.AbsoluteVelocity)[1])
                 << ']';
        else
          report << "null";
        report << ",\"history\":[";
        for (size_t j = 0; j < component.HeldHistory.size(); ++j) {
          if (j) report << ',';
          report << int(component.HeldHistory[j]);
        }
        report << "]}";
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
        auto poseInput = CurrentSceneCharacterPoseState(actor, blink->Phase, mouths.at(id).Frame,
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
      report << "],\"voice_queue\":{\"next_slot\":" << voiceOwner->Queue.NextSlot
             << ",\"prepared\":[";
      for (size_t i = 0; i < voiceOwner->Queue.Prepared.size(); ++i) {
        if (i) report << ',';
        const auto& prepared = voiceOwner->Queue.Prepared[i];
        report << '[' << prepared.VoiceId << ',' << prepared.Slot << ']';
      }
      report << "]},\"scene_pose_inputs\":[";
      firstInput = true;
      for (auto id : SceneCharacterDrawOrder(state)) {
        const auto& actor = state.Characters.at(id);
        const auto poseInput = CurrentSceneCharacterPoseState(
            actor, vm->SpriteBlink(id)->Phase, mouths.at(id).Frame, *state.Quake[1]);
        if (!firstInput) report << ',';
        firstInput = false;
        const auto* retained = actor.Pose.PreviousResourceSlot
            ? poses.Get(*actor.Pose.PreviousResourceSlot) : nullptr;
        const auto* prepared = actor.Pose.ResourceSlot
            ? poses.Get(*actor.Pose.ResourceSlot) : nullptr;
        const uint32_t rgb = uint32_t(poseInput.Color[0]) |
            (uint32_t(poseInput.Color[1]) << 8) | (uint32_t(poseInput.Color[2]) << 16);
        // The requested pose tag changes one update before its resource installs.
        report << '[' << (prepared ? prepared->Pose : -1) << ',' << (retained ? retained->Pose : -1)
               << ',' << poseInput.Face << ',' << actor.Pose.PreviousFace << ','
               << poseInput.EyePhase << ',' << poseInput.MouthFrame << ',' << poseInput.ScalePercent
               << ',' << poseInput.Position[0] << ',' << poseInput.Position[1] << ',' << rgb
               << ',' << actor.Tweens[size_t(CharacterTween::Opacity)].Current
               << ',' << actor.Tweens[size_t(CharacterTween::PreviousPose)].Current
               << ',' << poseInput.Visible << ']';
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
    if (!quit || firstMessage == UINT64_MAX || (captureUntilMessage && !targetCompleted))
      throw UnsupportedCommand("scene preview budget exhausted");
  }
  return 0;
}
}  // namespace

int RunScreenFilterProbe(const std::filesystem::path& fixtureDirectory,
                         const std::filesystem::path& output) {
  try {
    if (std::filesystem::exists(output)) throw FormatError("choose a fresh filter probe output");
    const auto bytes = SceneRead(fixtureDirectory / "cases.bin");
    PayloadReader reader(bytes);
    if (reader.U32() != 0x31504653) throw FormatError("invalid filter probe fixture");
    const auto count = reader.U32();
    if (!count || count > 256) throw FormatError("filter probe case budget exceeded");
    reader.CheckCount(count, 20);
    const auto fragmentBytes = SceneRead(fixtureDirectory / "reference.frag");
    const std::string fragment(fragmentBytes.begin(), fragmentBytes.end());
    ViewerWindow window(true);
    UiBlitRenderer actual, reference(fragment.c_str());
    struct Target {
      GLuint Framebuffer = 0, Texture = 0;
      ~Target() {
        if (Framebuffer) glDeleteFramebuffers(1, &Framebuffer);
        if (Texture) glDeleteTextures(1, &Texture);
      }
    } target;
    glGenFramebuffers(1, &target.Framebuffer);
    glGenTextures(1, &target.Texture);
    glBindTexture(GL_TEXTURE_2D, target.Texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.Texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw FormatError("filter probe float framebuffer is incomplete");
    std::ofstream report(output);
    if (!report) throw FormatError("cannot create filter probe output");
    for (uint32_t i = 0; i < count; ++i) {
      const auto pixel = reader.U32(), color = reader.U32(), mode = reader.U32();
      const auto value = std::bit_cast<float>(reader.U32());
      const auto kind = reader.U32();
      if (kind > 3) throw FormatError("invalid filter probe primitive");
      const ProfileTextureKey key{ProfileTextureSection::MainUi, -1, i};
      const ImagePixels pixels{1, 1, {uint8_t(pixel), uint8_t(pixel >> 8), uint8_t(pixel >> 16), uint8_t(pixel >> 24)}};
      UiSpriteBlit draw;
      draw.Texture = key; draw.Destination = {0,0,960,544}; draw.Source = {0,0,1,1};
      draw.Color = {uint8_t(color), uint8_t(color >> 8), uint8_t(color >> 16), uint8_t(color >> 24)};
      const auto sample = [&](UiBlitRenderer& renderer) {
        renderer.Upload(key, pixels);
        renderer.Draw({}, 960, 544);
        glViewport(0,0,1,1); glDisable(GL_BLEND);
        renderer.SetFilter(1, 1.0f);  // Also exercise a real reset to Normal.
        renderer.SetFilter(int32_t(mode), value);
        if (kind == 3) glUniform1i(renderer.AlphaMode, 0);  // Base native GXP family.
        renderer.DrawQuad(draw, renderer.Textures.at(key), kind == 2, kind == 1,
                          kind == 3 ? std::optional<float>(value) : std::nullopt);
        std::array<float,4> result;
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result.data());
        GlCheck("offscreen filter sample");
        return result;
      };
      const auto obtained = sample(actual), expected = sample(reference);
      report << "{\"case\":" << i << ",\"actual_bits\":[";
      for (size_t c = 0; c < 4; ++c) { if (c) report << ','; report << std::bit_cast<uint32_t>(obtained[c]); }
      report << "],\"reference_bits\":[";
      for (size_t c = 0; c < 4; ++c) { if (c) report << ','; report << std::bit_cast<uint32_t>(expected[c]); }
      report << "]}\n";
    }
    return 0;
  } catch (const std::exception& error) {
    const auto diagnostic = output.string() + ".error.txt";
    if (!std::filesystem::exists(diagnostic)) {
      std::ofstream failure(diagnostic);
      failure << std::string(error.what()).substr(0, 2048) << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano filter probe: %s", error.what());
    return 1;
  }
}
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
      InitializeFreshCgHistory(*flowOwner, LoadAlbumConfiguration(read(root / "first.dat"),
          read(root / "env/env_omake.dat")).RequiredImages);
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
                        const std::filesystem::path& captureDirectory,
                        bool captureAuto, bool captureButtonAuto, bool captureVoiced,
                        std::optional<size_t> captureUntilMessage,
                        std::optional<size_t> captureFromCommand) {
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
    InitializeFreshCgHistory(owner, LoadAlbumConfiguration(SceneRead(usrdir / "first.dat"),
        SceneRead(usrdir / "env/env_omake.dat")).RequiredImages);
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
                             usrdir, captureDirectory, captureAuto, captureButtonAuto, captureVoiced,
                             captureUntilMessage, captureFromCommand);
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
