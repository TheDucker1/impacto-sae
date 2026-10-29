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
#include "particle.h"
#include "scenecamera.h"
#include "approachresulttext.h"
#include "particlevertexshader.h"
#include "particlerastervertexshader.h"
#include "particlefragmentshader.h"
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
GLenum ParticleGlFactor(uint8_t factor, bool alpha) {
  switch (factor) {
    case 0: return GL_ZERO;
    case 1: return GL_ONE;
    case 2: return alpha ? GL_SRC_ALPHA : GL_SRC_COLOR;
    case 4: return GL_SRC_ALPHA;
    case 5: return GL_ONE_MINUS_SRC_ALPHA;
    default: throw FormatError("unsupported particle blend factor");
  }
}
void ApplyParticleBlend(size_t index) {
  const auto blend = ParticleBlend(index);
  if (!blend.Enabled) { glDisable(GL_BLEND); return; }
  glEnable(GL_BLEND);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  glBlendFuncSeparate(ParticleGlFactor(blend.ColorSource, false),
                      ParticleGlFactor(blend.ColorDestination, false),
                      ParticleGlFactor(blend.AlphaSource, true),
                      ParticleGlFactor(blend.AlphaDestination, true));
}
void ApplyParticleFragmentState(const ParticleFragmentState& state, GLuint program) {
  // The owning renderer selects ParticleFragmentShaders[state.AlphaFamily].
  if (state.AlphaFamily > 6 || !std::isfinite(state.AlphaThreshold))
    throw FormatError("invalid particle alpha state");
  ApplyParticleBlend(state.BlendIndex);
  glUniform1f(glGetUniformLocation(program, "nativeAlphaThreshold"), state.AlphaThreshold);
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
  void Bind(Cubism2Model& model) const {
    for(size_t i=0;i<Handles.size();++i)model.BindTexture(i,Handles[i]);
  }
};

// Cubism2 owns a legacy GL pipeline. Keep its client arrays on VAO0 and restore
// the UI pipeline, including fixed-function state changed by the SDK.
struct ScopedSdkGlState {
  using Push=void(APIENTRY*)(GLbitfield);
  using Pop=void(APIENTRY*)();
  Pop PopAttributes=nullptr,PopClientAttributes=nullptr;
  GLint Program=0,Vao=0,ArrayBuffer=0,ActiveTexture=0;
  ScopedSdkGlState() {
    // The app's GLAD surface is core-only; resolve the compatibility functions
    // from the compatibility context created for the legacy SDK.
    const auto push=reinterpret_cast<Push>(SDL_GL_GetProcAddress("glPushAttrib"));
    const auto pushClient=reinterpret_cast<Push>(SDL_GL_GetProcAddress("glPushClientAttrib"));
    PopAttributes=reinterpret_cast<Pop>(SDL_GL_GetProcAddress("glPopAttrib"));
    PopClientAttributes=reinterpret_cast<Pop>(SDL_GL_GetProcAddress("glPopClientAttrib"));
    if(!push || !pushClient || !PopAttributes || !PopClientAttributes)
      throw UnsupportedCommand("SDK compatibility GL state functions are unavailable");
    glGetIntegerv(GL_CURRENT_PROGRAM,&Program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&Vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&ArrayBuffer);glGetIntegerv(GL_ACTIVE_TEXTURE,&ActiveTexture);
    push(0x000fffff);glBindVertexArray(0);  // GL_ALL_ATTRIB_BITS.
    pushClient(0xffffffff);glUseProgram(0);glActiveTexture(GL_TEXTURE0);
  }
  ~ScopedSdkGlState() {
    PopClientAttributes();PopAttributes();glUseProgram(GLuint(Program));
    glBindVertexArray(GLuint(Vao));glBindBuffer(GL_ARRAY_BUFFER,GLuint(ArrayBuffer));
    glActiveTexture(GLenum(ActiveTexture));
  }
};
std::vector<GLint> UiGlState() {
  std::vector<GLint> result;
  for(const GLenum key:{GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ARRAY_BUFFER_BINDING,
      GL_ELEMENT_ARRAY_BUFFER_BINDING,GL_ACTIVE_TEXTURE,GL_TEXTURE_BINDING_2D,
      GL_BLEND_SRC_RGB,GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,
      GL_BLEND_EQUATION_RGB,GL_BLEND_EQUATION_ALPHA,GL_DEPTH_WRITEMASK,
      GL_UNPACK_ALIGNMENT}) {
    GLint value=0;glGetIntegerv(key,&value);result.push_back(value);
  }
  for(const GLenum key:{GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,
      GL_SCISSOR_TEST,0x0bc0,GL_FRAMEBUFFER_SRGB})result.push_back(glIsEnabled(key)); // GL_ALPHA_TEST.
  std::array<GLint,4> viewport{};glGetIntegerv(GL_VIEWPORT,viewport.data());
  result.insert(result.end(),viewport.begin(),viewport.end());return result;
}
void DrawSceneModels(SdkLive2dDriver& models,const Live2dState& state,
    std::map<int32_t,std::unique_ptr<ModelTextures>>& textures,bool verifyState) {
  if(state.Records.empty())return;
  const auto before=verifyState?UiGlState():std::vector<GLint>{};
  {
    ScopedSdkGlState scope;
    for(const auto& record:state.Records) {
      if(!record.ResourceId)continue;
      const auto id=*record.ResourceId;
      if(!textures.contains(id))textures[id]=std::make_unique<ModelTextures>(models.Model(id),models.Assets(id));
      // The SDK object may have been reattached since the texture upload.
      textures.at(id)->Bind(models.Model(id));
    }
    models.Draw(state);
  }
  GlCheck("scene SDK draw/state restoration");
  if(verifyState && UiGlState()!=before)throw FormatError("SDK drawing disturbed UI OpenGL state");
}

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
  if(command.TestEnabled)out << ",\"enabled\":" << *command.TestEnabled;
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
    // ScenarioGameWrapper::Draw (8003110e) clears RGBA ffffffff before HUD drawing.
    // The native chapter loader capture also observes this clear, before 800764e4.
    glClearColor(1, 1, 1, 1);
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
          glUniform1i(AlphaMode, command.TestEnabled.value_or(true)?command.Mode:0);
          break;
        case UiRenderKind::DepthTest:
          if (command.Mode != 0 && command.Mode != 5)
            throw UnsupportedCommand("unsupported profile depth comparison");
          if(command.TestEnabled.value_or(true)) {
            glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
            glDepthFunc(command.Mode == 0 ? GL_NEVER : GL_LEQUAL);
          } else {
            glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDepthFunc(GL_ALWAYS);
          }
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
struct ParticleGlRenderer {
  const ParticleManager* TextureOwner = nullptr;
  uint64_t TextureGeneration = 0;
  std::map<size_t,UiBlitRenderer::BoundTexture> Textures;
  GLuint Vao = 0, Buffer = 0;
  std::array<GLuint,7> Programs{};
  ParticleGlRenderer() {
    glGenVertexArrays(1,&Vao);glGenBuffers(1,&Buffer);
  }
  ~ParticleGlRenderer() {
    ResetTextures();
    for (auto program : Programs) if (program) glDeleteProgram(program);
    if (Buffer) glDeleteBuffers(1,&Buffer);
    if (Vao) glDeleteVertexArrays(1,&Vao);
  }
  void ResetTextures() {
    for (const auto& [index,texture] : Textures) glDeleteTextures(1,&texture.Handle);
    Textures.clear(); TextureOwner=nullptr;
  }
  const UiBlitRenderer::BoundTexture& Texture(const ParticleManager& manager,size_t index) {
    if (TextureOwner!=&manager || TextureGeneration!=manager.ResourceGeneration()) {
      ResetTextures();TextureOwner=&manager;TextureGeneration=manager.ResourceGeneration();
    }
    if (const auto found=Textures.find(index);found!=Textures.end()) return found->second;
    ScopedSdkGlState scope;
    const auto pixels=DecodeSceneImage(manager.TextureData(index)); GLuint handle=0;glGenTextures(1,&handle);
    const auto found=Textures.emplace(index,UiBlitRenderer::BoundTexture{handle,pixels.Width,pixels.Height}).first;
    glBindTexture(GL_TEXTURE_2D,handle);
    // Original b5cb2's final setters: LINEAR min/mag and CLAMP U/V.
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,pixels.Width,pixels.Height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.Rgba.data());
    GlCheck("particle GXT upload");return found->second;
  }
  size_t DrawGroup(const ParticleManager& manager,size_t group,float opacity,
      const ParticleProjectionUniforms& projection,const ParticleFragmentState& fragment,
      const ParticleRasterViewport& viewport) {
    std::vector<std::array<uint32_t,2>> sizes(manager.TextureOrder().size());
    for (const bool batched : {true,false}) for (const auto& source : manager.CollectDrawSources(group,batched)) {
      if (!source.Particle->Birth.Flags[0] || !source.TextureIndex) continue;
      const auto index=*source.TextureIndex;const auto& texture=Texture(manager,index);
      sizes[index]={texture.Width,texture.Height};
    }
    const auto draws=manager.BuildDrawCommands(group,sizes,opacity);
    for (const auto& draw : draws) {
      if (draw.Batched) throw UnsupportedCommand("batched particle GPU submission is not bound");
      const auto& texture=Texture(manager,draw.TextureIndex);
      Draw(draw.Packet,texture.Handle,texture.Width,texture.Height,projection,fragment,viewport);
    }
    return draws.size();
  }
  GLuint Program(uint32_t family) {
    if (family >= Programs.size()) throw FormatError("unknown particle fragment family");
    if (Programs[family]) return Programs[family];
    const auto vertex = UiBlitRenderer::Compile(GL_VERTEX_SHADER,ParticleMode0RasterVertexShader);
    GLuint fragment = 0, program = 0;
    try {
      fragment=UiBlitRenderer::Compile(GL_FRAGMENT_SHADER,ParticleSceneFragmentShaders[family]);
      program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);
      glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
      if (!linked) { char log[1024]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);throw FormatError(log); }
    } catch (...) {
      if (program) glDeleteProgram(program);if (fragment) glDeleteShader(fragment);glDeleteShader(vertex);throw;
    }
    glDeleteShader(vertex);glDeleteShader(fragment);Programs[family]=program;return program;
  }
  void Draw(const ParticleDrawPacket& packet, GLuint texture, uint32_t textureWidth,
            uint32_t textureHeight, const ParticleProjectionUniforms& projection,
            const ParticleFragmentState& fragment, const ParticleRasterViewport& viewport) {
    if (packet.RenderMode != 0) throw UnsupportedCommand("particle shader mode is not implemented");
    if (!texture) throw FormatError("particle draw has no texture");
    if (!(viewport.TargetSize[0]>0) || !(viewport.TargetSize[1]>0))
      throw FormatError("particle target size is invalid");
    // Native98c8's mode0 reset supersedes the earlier intensity filter setup.
    // Keep the PC UI's physical pipeline intact; logical native state is owned
    // separately by the scene. This scope does not touch the target framebuffer.
    ScopedSdkGlState scope;
    const auto program=Program(fragment.AlphaFamily);glUseProgram(program);
    const auto uniforms=projection.Pack();
    glUniform4fv(glGetUniformLocation(program,"nativeUniforms"),13,uniforms.data());
    glUniform4fv(glGetUniformLocation(program,"nativeViewportFlip"),1,viewport.Flip.data());
    glUniform2fv(glGetUniformLocation(program,"nativeTargetSize"),1,viewport.TargetSize.data());
    glUniform1f(glGetUniformLocation(program,"viewportEnabled"),viewport.Enabled);
    glUniform1f(glGetUniformLocation(program,"nativeDepthOffset"),viewport.DepthOffset);
    glUniform1f(glGetUniformLocation(program,"nativeDepthScale"),viewport.DepthScale);
    glUniform1i(glGetUniformLocation(program,"presentToPc"),viewport.PresentToPc);
    glUniform1i(glGetUniformLocation(program,"image"),0);
    ApplyParticleFragmentState(fragment,program);
    glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
    const auto vertices=BuildParticleQuad(packet,textureWidth,textureHeight);
    glBindVertexArray(Vao);glBindBuffer(GL_ARRAY_BUFFER,Buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_STREAM_DRAW);
    constexpr GLint components[]{3,4,2,3,3};constexpr size_t offsets[]{0,12,16,24,36};
    for (GLuint index=0;index<5;++index) {
      glEnableVertexAttribArray(index);
      glVertexAttribPointer(index,components[index],index==1?GL_UNSIGNED_BYTE:GL_FLOAT,
          index==1?GL_TRUE:GL_FALSE,sizeof(ParticleVertex),reinterpret_cast<void*>(offsets[index]));
    }
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);GlCheck("particle raster drawing");
  }
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
#include "desktopprofile.inc"

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
                      std::optional<size_t> captureFromCommand = {},
                      std::optional<int32_t> captureScriptOverride = {},
                      bool captureWaitingInput = false,bool captureConfirmInput = false,int32_t captureMenuInput = 0,
                      const std::filesystem::path& capturePlan = {}) {
  const bool capture = !captureDirectory.empty();
  const bool captureParticles=captureMenuInput==4;
  const bool captureChapter=captureMenuInput==7;
  const bool captureLip=captureMenuInput==9;
  const bool captureBattle=captureMenuInput==10;
  const bool captureVersus=captureMenuInput==11;
  const bool captureProfile=captureMenuInput==8 || captureLip || captureBattle || captureVersus;
  const bool captureResult=captureMenuInput==5 || captureMenuInput==6 || captureChapter || captureProfile;
  const bool captureResultPointer=captureMenuInput==6;
  std::vector<std::array<size_t,2>> choicePlan;size_t choicePlanIndex=0;
  std::array<size_t,2> profileChoice{};
  if(captureProfile) {
    std::ifstream plan(capturePlan);size_t extra=0;
    if(!(plan>>profileChoice[0]>>profileChoice[1]) || profileChoice[0]>=4 || profileChoice[1]>=10 || (plan>>extra) || !plan.eof())
      throw FormatError("profile capture requires one explicit ordinal/selector plan row");
  }
  if(!capturePlan.empty() && !captureProfile) {
    if(!capture || !captureResult)throw FormatError("choice plan requires a result capture");
    std::ifstream plan(capturePlan);if(!plan)throw FormatError("cannot read capture choice plan");
    size_t parent=0,child=0;
    while(plan>>parent) {
      if(!(plan>>child) || parent>=6 || child>=3 || choicePlan.size()>=64)
        throw FormatError("invalid capture choice plan row");
      choicePlan.push_back({parent,child});
    }
    if(!plan.eof() || choicePlan.empty())throw FormatError("invalid capture choice plan");
  }
  const bool captureMenuPointer=captureMenuInput==2,captureBranch=captureMenuInput==3 || captureParticles || captureResult;
  if (!owner.WindowOpacity || !owner.ReadColorEnabled || !owner.TextSpeed ||
      !owner.AutoSpeed)
    throw UnsupportedCommand("scene owner display settings are missing");
  const auto opacity = *owner.WindowOpacity;
  const auto format = TextFormatEnvironment::Decode(
      SceneRead(root / "env/env_string_format.dat"), true);
  const auto timing = TextTimingEnvironment::Decode(
      SceneRead(root / "env/env_config.dat"), true, true);
  const auto visual = OrdinaryTextEnvironment::Decode(
      SceneRead(root / "env/env_message_window.dat"),true);
  const auto inputEnvironment = WidgetInputEnvironment::DecodeUtility(
      SceneRead(root / "env/env_game_utility.dat"));
  OrdinaryWidgetInput sceneInput(visual.Window, inputEnvironment.HoldFrames);
  WidgetInputHistory resultPointer;
  WidgetEventHistory resultTouch(inputEnvironment.HoldFrames);
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
  const auto chapterVisual=ChapterTitleVisualEnvironment::Decode(SceneRead(root/"env/env_script.dat"),SceneRead(root/"env/env_system_menu.dat"));
  ArchiveSceneImages images(resources);
  ArchiveCharacterPoses poses(resources);
  ArchiveApproachResources approachAssets(resources);
  ArchiveBattleResources battleAssets(resources);
  size_t battleUploadsChecked=0;
  battleAssets.Upload=[&](const PreparedProfileTexture& texture){
    renderer.Upload(texture.Key,texture.Pixels);
    if(captureBattle) {
      std::vector<uint8_t> actual(texture.Pixels.Rgba.size());GLint previousTexture=0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
      glBindTexture(GL_TEXTURE_2D,renderer.Textures.at(texture.Key).Handle);
      glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
      glBindTexture(GL_TEXTURE_2D,GLuint(previousTexture));GlCheck("battle texture storage readback");
      if(actual!=texture.Pixels.Rgba)throw FormatError("battle texture upload differs from decoded source");
      ++battleUploadsChecked;
    }
  };
  const auto approachVisual=ApproachVisualEnvironment::Decode(SceneRead(root/"env/env_approach.dat"));
  const auto particleEnvironment=ParticleEnvironment::Decode(SceneRead(root/"env/env_particle.dat"));
  ParticleManager particles(0); ParticleGlRenderer particleRenderer;
  auto particleCamera=InitialSceneCamera();
  ParticleFragmentState particleFragment;particleFragment.SetBlendMode(1);
  uint64_t particleClock=0;
  bool particleCaptureRequested=false;size_t particleCaptureFrames=0;
  struct ParticleRequestLog { int32_t Id;ParticleEffectRequest Request;uint32_t Seed;uint64_t Before; };
  std::vector<ParticleRequestLog> particleRequests;
  std::set<int32_t> sessionTrophies;
  const auto selectionLayout=ApproachSelectionLayout::Decode(SceneRead(root/"env/env_approach.dat"));
  const auto selectionShared=SharedSelectionEnvironment::Decode(SceneRead(root/"env/env_script_select.dat"));
  std::map<int32_t,std::unique_ptr<ModelTextures>> modelTextures;
  SdkLive2dDriver models(resources, Live2dEnvironment::Decode(SceneRead(root / "env/env_live2d.dat")));
  const auto voiceSettings = MakeFreshOrdinaryVoiceSettings();
  PlaybackMissionVoice missionVoice(playback,root,voiceSettings.Volume,1.0f);
  const auto resultVisual=ModeResultVisualEnvironment::DecodeApproach(SceneRead(root/"env/env_approach.dat"));
  const auto resultCursors=MessageCursorEnvironment::Decode(SceneRead(root/"env/env_message_window.dat"));
  ArchiveApproachResultHost resultHost(resources,missionVoice,skipAuto,format,timing,
      resultVisual,font.Cached(),metrics,uint32_t(utility.Size),font.NominalSize());
  SceneMessageBackend backend(format, timing, *owner.TextSpeed, owner.AutoSpeed);
  PlaybackProfileVoice profileVoice(playback,10,1.0f);
  ArchiveProfileServices profileServices(resources,ScriptNameIndex::FromFirstArchive(SceneRead(root/"first.dat")),
      format,timing,&profileVoice);
  SceneProfilePresenter profilePresenter(root,resources,renderer,fontTextures,font,profileServices,profileVoice,playback);
  backend.ProfileConsumer=&profileServices;backend.ProfileSettings=profilePresenter.settings;
  backend.ModeResultSettings=profilePresenter.shared;
  backend.ApproachResultSettings=ApproachResultEnvironment::Decode(SceneRead(root/"env/env_approach.dat"));
  backend.ResultConsumer=&resultHost;
  backend.ChapterSettings=ChapterEnvironment::Decode(SceneRead(root/"env/env_script.dat"));
  backend.ParticleReset=[&](bool fresh) {
    if(!fresh)throw UnsupportedCommand("restored particle manager state is unavailable");
    particles.ResetForScript(capture?particleClock:SDL_GetTicks());
    particles.LoadEffect(resources,particleEnvironment,10);
    particles.LoadEffect(resources,particleEnvironment,11);
  };
  backend.ParticleRequest=[&](const GameEvent& event) {
    if(event.Id!=10 && event.Id!=11)throw UnsupportedCommand("scene particle request type is unbound");
    const auto request=BuildApproachParticleRequest(event.ScreenPosition,particleCamera,particleEnvironment.Scalar);
    particleRequests.push_back({event.Id,request,particles.Random().SeedValue(),particles.Random().Draws()});
    if(!particles.Request(size_t(event.Id),request,particleEnvironment.Effects.at(size_t(event.Id)).Parameter))
      throw UnsupportedCommand("scene particle resource is not registered");
    particleCaptureRequested=true;
  };
  backend.SceneImages = &images;
  backend.CharacterPoses = &poses;
  backend.ApproachResourceBank = &approachAssets;
  backend.BattleResourceBank=&battleAssets;
  backend.BattleConfig=BattleGaugeEnvironment::Decode(SceneRead(root/"env/env_tukkomi.dat"));
  backend.BattleVersusSettings=BattleVersusEnvironment::Decode(SceneRead(root/"env/env_tukkomi.dat"));
  const auto versusVisual=BattleVersusVisualEnvironment::Decode(SceneRead(root/"env/env_tukkomi.dat"));
  backend.SceneModels = &models;
  backend.MissionVoiceBank=&missionVoice;
  backend.MissionSettings=ApproachMissionEnvironment::Decode(SceneRead(root/"env/env_approach.dat"));
  backend.SelectionSettings=ApproachSelectionEnvironment::Decode(SceneRead(root/"env/env_approach.dat"));
  backend.ApproachConfig = ApproachGaugeEnvironment::Decode(SceneRead(root / "env/env_approach.dat"));
  backend.MusicResources = &music;
  backend.SeArchives = &sounds;
  auto vm = VirtualMachine::FromCompletedProfile(resources, backend,
                                                 std::move(owner), completed);
  const auto captureScript = captureScriptOverride.value_or(vm->State().ScriptFileId);
  if (captureFromCommand && (!captureUntilMessage || *captureFromCommand > *captureUntilMessage))
    throw FormatError("capture range requires a start before its target command");
  if (captureUntilMessage) {
    const ScriptObject script(resources.Load(captureScript));
    if (*captureUntilMessage >= script.Records().size() ||
        (!captureFromCommand && script.Records()[*captureUntilMessage].Opcode != 0x64))
      throw FormatError("capture target is not a valid entry-script command/message");
    if (!capture || !captureAuto)
      throw FormatError("capture target requires automatic capture mode");
    if(captureMenuInput && script.Records()[*captureUntilMessage].Opcode!=(captureBranch?0x64:0x3f6))
      throw FormatError("menu capture target requires approach selection");
  }
  bool targetCompleted = false, targetWaitingReached = false;
  bool captureActive = !captureFromCommand && !captureConfirmInput && !captureParticles && !captureResult;
  bool resultConfirmationDue=false,resultConfirmationSent=false,resultCompleted=false;
  bool chapterCompleted=false;
  bool profileActive=false,profileReturned=false;
  std::ofstream profileFrames;
  std::string profileSignature;uint64_t profileLastSaved=0;size_t profileSaved=0;
  size_t resultPointerCaptureStage=0;
  bool confirmDue=false,confirmSent=false;
  bool menuMissionDue=false,menuMissionSent=false;
  uint32_t menuCaptureStep=0;
  std::pair<int32_t,int32_t> captureMenuSite{-1,-1};
  std::optional<uint64_t> pcmEpoch;
  std::map<uint16_t, ProfileMouthPlayback> mouths;
  SpriteMessageMouth spriteMouth(vm->State(), mouths,&models);
  PlaybackOrdinaryVoice voices(playback, root, voiceSettings.Volume, 1.0f);
  std::unique_ptr<SceneMessageVoiceOwner> voiceOwner;
  int32_t voiceScript = -1;
  const auto bindScriptVoices = [&] {
    if (voiceScript == vm->State().ScriptFileId) return;
    // File load creates a fresh manager RNG. The headless route supplies this
    // same host seed at that boundary; PC must also seed before sprite updates.
    if(!vm->State().Random.Known())vm->SeedRandom(capture?0u:uint32_t(SDL_GetTicks()));
    backend.ResetMessages();
    mouths.clear();
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
    if(captureProfile && !captureLip && !captureBattle && !captureVersus){profileFrames.open(captureDirectory/"profile-frames.jsonl");if(!profileFrames)throw FormatError("cannot create profile frame output");}
  }
  const auto drawSprite = [&](const UiSpriteBlit& sprite) {
    renderer.DrawQuad(sprite, renderer.Textures.at(sprite.Texture), false);
  };
  backend.ActivateLane = [&](uint32_t lane, uint64_t time, int32_t position) {
    const auto before = vm->State().LaneCommandIndex.at(lane);
    vm->ActivateLane(lane);
    if (capture && captureActive) {
      events << "{\"kind\":\"inline_queue\",\"now\":" << time << ",\"command\":"
             << vm->State().CurrentCommandIndex << ",\"lane\":" << lane << ",\"voice_position\":"
             << position << ",\"before\":" << before << ",\"after\":"
             << vm->State().LaneCommandIndex.at(lane) << ",\"queued_commands\":[";
      const auto queued = vm->QueuedLaneCommands(lane);
      for (size_t i = 0; i < queued.size(); ++i) { if (i) events << ','; events << queued[i]; }
      events << "]}\n";
    }
  };
  const auto upload = [&](ProfileTextureKey key, const ImagePixels& pixels) {
    if (!renderer.Textures.contains(key)) renderer.Upload(key, pixels);
  };
  const auto uploadMain = [&](uint32_t id) {
    const ProfileTextureKey key{ProfileTextureSection::MainUi, -1, id};
    if (!renderer.Textures.contains(key))
      upload(key, DecodeSceneImage(resources.LoadProfileTexture(key).Data));
  };
  size_t versusUploadsChecked=0;
  const auto uploadBattleUi=[&](uint32_t id) {
    const ProfileTextureKey key{ProfileTextureSection::BattleUi,-1,id};
    if(renderer.Textures.contains(key))return;
    const auto pixels=DecodeSceneImage(resources.LoadProfileTexture(key).Data);upload(key,pixels);
    if(captureVersus) {
      std::vector<uint8_t> actual(pixels.Rgba.size());GLint previousTexture=0;
      glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);glBindTexture(GL_TEXTURE_2D,renderer.Textures.at(key).Handle);
      glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
      glBindTexture(GL_TEXTURE_2D,GLuint(previousTexture));GlCheck("versus atlas storage readback");
      if(actual!=pixels.Rgba)throw FormatError("versus atlas upload differs from decoded source");
      ++versusUploadsChecked;
    }
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
  bool versusConfirmSent=false;
  std::map<uint16_t, bool> actorEligibility;
  WidgetInputHistory menuPointer;
  std::vector<WidgetEventHistory> menuParents,menuChildren,menuArrows;
  std::pair<int32_t,int32_t> menuIdentity{-1,-1};
  int32_t menuChildParent=-1;
  const uint32_t frameBudget = (captureChapter || captureProfile)?220000:(captureParticles || captureResult)?100000:(captureFromCommand ? (captureBranch?40000:20000) : 2000);
  for (uint32_t frame = 0; !quit && (!capture || frame < frameBudget); ++frame) {
    const uint64_t now =
        capture ? uint64_t(frame) * 16 : SDL_GetTicks() - epoch;
    particleClock=now;
    messageCursor.Update(now);
    CommandInput input;
    SkipAutoInput policyInput;
    SDL_Event event;
    if(captureVersus && vm->State().BattleVersus.Phase==12 && !vm->State().BattleVersus.PendingPhase && !versusConfirmSent) {
      for(const auto type:{SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP}) {
        SDL_Event e{};e.type=type;e.key.windowID=SDL_GetWindowID(window.Window);e.key.key=SDLK_RETURN;
        e.key.down=type==SDL_EVENT_KEY_DOWN;SdlCheck(SDL_PushEvent(&e),"versus capture explicit confirm");
      }
      versusConfirmSent=true;events << "{\"kind\":\"battle_versus_confirmation\",\"now\":" << now << ",\"source\":\"explicit_capture_SDL_key\"}\n";
    }
    if(captureProfile && vm->CurrentProfile()) {
      const auto key=profilePresenter.CaptureKey(*vm->CurrentProfile(),profileChoice,now);
      if(key)for(const auto type:{SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP}) {
        SDL_Event e{};e.type=type;e.key.windowID=SDL_GetWindowID(window.Window);e.key.key=key;e.key.down=type==SDL_EVENT_KEY_DOWN;
        SdlCheck(SDL_PushEvent(&e),"profile capture explicit keyboard input");
        if(type==SDL_EVENT_KEY_DOWN)events << "{\"kind\":\"profile_input\",\"now\":" << now << ",\"source\":\"SDL_key\",\"key\":" << key << "}\n";
      }
    }
    if(capture && captureMenuInput) {
      const auto& menu=vm->State().ApproachSelection;
      if(captureBranch && menu.Visible) {
        const auto site=std::pair{vm->State().ScriptFileId,vm->State().CurrentCommandIndex};
        if(site!=captureMenuSite || (menuCaptureStep>=4 && menu.Phase==4 && menu.ParentUnselected)) {
          captureMenuSite=site;menuCaptureStep=0;
        }
      }
      SDL_Keycode key=0;
      if(resultConfirmationDue && !captureResultPointer){key=SDLK_RETURN;resultConfirmationDue=false;resultConfirmationSent=true;}
      else if(menuMissionDue){key=SDLK_RETURN;menuMissionDue=false;menuMissionSent=true;}
      else if(!captureMenuPointer && menu.Visible && vm->State().ScriptFileId==captureScript &&
          (captureBranch || size_t(vm->State().CurrentCommandIndex)==*captureUntilMessage) && !menu.PendingPhase &&
          (choicePlan.empty() || menu.Phase==4 || menu.Phase==7)) {
        if(!choicePlan.empty()) {
          if(choicePlanIndex>=choicePlan.size())throw FormatError("capture choice plan exhausted");
          const auto target=choicePlan.at(choicePlanIndex);
          const auto found=std::find(menu.OriginalOrdinals.begin(),menu.OriginalOrdinals.end(),target[0]);
          if(found==menu.OriginalOrdinals.end() || target[1]>=menu.ChildCounts.at(size_t(found-menu.OriginalOrdinals.begin())) ||
              vm->State().ApproachFlags.at(target[0]*3+target[1]))throw FormatError("capture choice is disabled or used");
          if(menu.Phase==4)key=menu.ParentUnselected || menu.Parent!=int32_t(found-menu.OriginalOrdinals.begin())?SDLK_DOWN:SDLK_RETURN;
          else if(menu.Phase==7)key=menu.ChildUnselected || menu.Child!=int32_t(target[1])?SDLK_DOWN:SDLK_RETURN;
        } else if(menuCaptureStep==0 && menu.Phase==4){key=SDLK_DOWN;++menuCaptureStep;}
        else if(menuCaptureStep==1 && menu.Phase==4) {
          bool available=true;
          if((captureParticles || captureResult) && !menu.ParentUnselected && menu.Parent>=0) {
            const auto row=size_t(menu.Parent),original=menu.OriginalOrdinals.at(row);
            available=false;
            for(size_t child=0;child<menu.ChildCounts.at(row);++child)
              available|=vm->State().ApproachFlags.at(original*3+child)==0;
          }
          if(!menu.ParentUnselected && available){key=SDLK_RETURN;++menuCaptureStep;}
          else if(frame%4==0)key=SDLK_DOWN;
        } else if(menuCaptureStep==2 && menu.Phase==7){key=SDLK_DOWN;++menuCaptureStep;}
        else if(menuCaptureStep==3 && menu.Phase==7) {
          const bool used=(captureParticles || captureResult) && !menu.ChildUnselected && menu.Parent>=0 && menu.Child>=0 &&
            vm->State().ApproachFlags.at(menu.OriginalOrdinals.at(size_t(menu.Parent))*3+size_t(menu.Child))!=0;
          if(!menu.ChildUnselected && !used){key=SDLK_RETURN;++menuCaptureStep;}
          else if(frame%4==0)key=SDLK_DOWN;
        }
      }
      if(key) {
        for(const auto type:{SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP}) {
          SDL_Event e{};e.type=type;e.key.windowID=SDL_GetWindowID(window.Window);
          e.key.key=key;e.key.down=type==SDL_EVENT_KEY_DOWN;SdlCheck(SDL_PushEvent(&e),"menu capture explicit key");
        }
        if(captureActive)events << "{\"kind\":\"menu_input\",\"source\":\"SDL_key\",\"key\":" << key
            << ",\"step\":" << menuCaptureStep << ",\"now\":" << now << "}\n";
      }
      if(captureMenuPointer && menu.Visible && !menu.PendingPhase && menuCaptureStep<4 &&
          ((menuCaptureStep<2 && menu.Phase==4) || (menuCaptureStep>=2 && menu.Phase==7))) {
        size_t children=menu.Parent>=0?menu.ChildCounts.at(size_t(menu.Parent)):0;
        const auto regions=BuildApproachSelectionHitRegions(selectionLayout,selectionShared,*backend.SelectionSettings,
            menu.OriginalOrdinals.size(),children);
        const auto& r=menuCaptureStep<2?regions.Parents.at(0):regions.Children.at(0);
        int w=0,h=0;SdlCheck(SDL_GetWindowSize(window.Window,&w,&h),"menu capture pointer viewport");
        SDL_Event e{};e.type=menuCaptureStep%2==0?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID=SDL_GetWindowID(window.Window);e.button.button=SDL_BUTTON_LEFT;e.button.down=menuCaptureStep%2==0;
        e.button.x=float(r[0]+r[2]/2)*float(w)/960;e.button.y=float(r[1]+r[3]/2)*float(h)/544;
        SdlCheck(SDL_PushEvent(&e),"menu capture explicit pointer");++menuCaptureStep;
        events << "{\"kind\":\"menu_pointer_input\",\"source\":\"SDL_mouse\",\"step\":" << menuCaptureStep
            << ",\"down\":" << e.button.down << ",\"now\":" << now << ",\"logical_position\":["
            << r[0]+r[2]/2 << ',' << r[1]+r[3]/2 << "]}\n";
      }
    }
    if(captureResultPointer && resultConfirmationDue) {
      int w=0,h=0;SdlCheck(SDL_GetWindowSize(window.Window,&w,&h),"result pointer capture viewport");
      SDL_Event e{};e.type=resultPointerCaptureStage==0?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
      e.button.windowID=SDL_GetWindowID(window.Window);e.button.button=SDL_BUTTON_LEFT;e.button.down=resultPointerCaptureStage==0;
      e.button.x=40.0f*float(w)/960;e.button.y=40.0f*float(h)/544;
      SdlCheck(SDL_PushEvent(&e),"result capture explicit pointer");
      events << "{\"kind\":\"result_pointer_input\",\"source\":\"SDL_mouse\",\"now\":" << now
        << ",\"down\":" << e.button.down << ",\"logical_position\":[40,40]}\n";
      if(++resultPointerCaptureStage==2){resultConfirmationDue=false;resultConfirmationSent=true;}
    }
    if(capture && confirmDue) {
      // Explicit capture input, delivered through the same SDL key handler as
      // a user's Enter press. Normal gameplay never fabricates confirmation.
      for(const auto type:{SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP}) {
        SDL_Event key{};key.type=type;key.key.windowID=SDL_GetWindowID(window.Window);
        key.key.key=SDLK_RETURN;key.key.down=type==SDL_EVENT_KEY_DOWN;
        SdlCheck(SDL_PushEvent(&key),"capture confirmation key");
      }
      confirmDue=false;confirmSent=true;
      events << "{\"kind\":\"explicit_confirmation\",\"source\":\"SDL_Enter\",\"now\":"
             << now << ",\"script\":" << captureScript << ",\"command\":" << *captureUntilMessage << "}\n";
    }
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
          (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE &&
           !vm->State().ApproachSelection.Visible))
        quit = true;
      if(vm->CurrentProfile()){profilePresenter.Handle(event,input.Profile);continue;}
      if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && vm->State().ApproachSelection.Visible) {
        input.Approach.Previous|=event.key.key==SDLK_UP;
        input.Approach.Next|=event.key.key==SDLK_DOWN;
        input.Approach.Back|=event.key.key==SDLK_ESCAPE || event.key.key==SDLK_BACKSPACE;
      }
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
        menuPointer={};menuParents.clear();menuChildren.clear();menuArrows.clear();menuIdentity={-1,-1};
      }
    }
    if (quit) break;
    // Dedicated fresh scene: no hidden-message mode or blocking overlay, and
    // no restored read history. History eligibility is an explicit false input
    // for this fresh-unread preview; a loaded-history owner must supply its lookup.
    // Policy consumes prior widget samples before this frame's updates.
    if(vm->CurrentProfile())profilePresenter.BeforeStep(input.Profile,now);
    if(!vm->CurrentProfile()) {
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
    if (capture && captureActive && policyFlags != previousPolicyFlags) {
      events << "{\"kind\":\"widget_policy\",\"frame\":" << frame
             << ",\"now\":" << now << ",\"flags\":" << policyFlags
             << ",\"velocity_x_bits\":" << std::bit_cast<uint32_t>(policyInput.PrimaryVelocityX)
             << ",\"mode\":" << vm->State().SkipMode << ",\"phase\":" << vm->State().SkipPhase
             << ",\"auto\":" << skipAuto.Auto << "}\n";
      previousPolicyFlags = policyFlags;
    }
    if (capture && captureActive && (policyInput.AutoPressed || wasAuto != skipAuto.Auto))
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
    const auto& menuState=vm->State().ApproachSelection;
    if(menuState.Visible) {
      const auto identity=std::pair{vm->State().ScriptFileId,vm->State().CurrentCommandIndex};
      const auto reset=[&](auto& components,size_t count){components.clear();
        for(size_t i=0;i<count;++i)components.emplace_back(inputEnvironment.HoldFrames);};
      if(menuIdentity!=identity) {
        menuIdentity=identity;menuPointer={};menuChildParent=-1;
        reset(menuParents,menuState.Carousel?1:menuState.OriginalOrdinals.size());reset(menuArrows,menuState.Carousel?2:0);
        sceneInput.CancelPointer();
      }
      size_t children=0;
      if(menuState.Parent>=0 && size_t(menuState.Parent)<menuState.ChildCounts.size())children=menuState.ChildCounts[size_t(menuState.Parent)];
      const auto regions=BuildApproachSelectionHitRegions(selectionLayout,selectionShared,*backend.SelectionSettings,
          menuState.OriginalOrdinals.size(),children);
      if(menuState.Phase==6 || (menuState.Phase==7 && menuChildParent!=menuState.Parent)) {
        reset(menuChildren,children);menuChildParent=menuState.Parent;
      }
      menuPointer.BeginFrame(pointerSnapshot);
      const auto released=[&](auto& components,const auto& rectangles) -> std::optional<size_t> {
        std::optional<size_t> hit;
        for(size_t i=0;i<components.size();++i) {
          auto& component=components[i];component.Update(SampleWidgetContact(rectangles.at(i),menuPointer.Current[0],
              menuPointer.Previous[0],component.TrackedId,menuPointer.Velocity(0)));
          menuPointer.Publish(component.Flags);if(!hit && (component.Flags&8))hit=i;
        }
        return hit;
      };
      if(menuState.Phase==4) {
        input.Approach.ParentReleased=released(menuParents,regions.Parents);
        if(const auto arrow=released(menuArrows,regions.Arrows)) {
          input.Approach.Previous|=*arrow==0;input.Approach.Next|=*arrow==1;
        }
      } else if(menuState.Phase==7)input.Approach.ChildReleased=released(menuChildren,regions.Children);
    } else {menuIdentity={-1,-1};menuChildParent=-1;}
    resultPointer.BeginFrame(pointerSnapshot);
    resultTouch.Update(SampleWidgetContact({0,0,960,544},resultPointer.Current[0],resultPointer.Previous[0],
        resultTouch.TrackedId,resultPointer.Velocity(0)));
    resultPointer.Publish(resultTouch.Flags);
    const bool ordinaryRelease=!menuState.Visible && sceneInput.UpdatePrimary();
    const bool resultInput=vm->Opcode()==0x3f8 || vm->Opcode()==0x3f9;
    const bool pointerAdvance=resultInput ? resultTouch.Enabled && (resultTouch.Flags&8) : ordinaryRelease;
    input.Advance = input.Advance || pointerAdvance;
    if (capture && captureActive) {
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
    battleAssets.Pump();
    for (size_t work = 0; work < 256; ++work) {
      bindScriptVoices();
      syncMouths();
      const bool acceptedInput=input.Advance;
      const auto step = vm->Step(now, input);
      if(!choicePlan.empty() && step.Opcode==0x3f6 && step.Boundary==VmBoundary::CommandCompletedBeforeDispatch) {
        const auto chosen=CompletedApproachChoice(vm->State().ApproachSelection);
        if(!chosen || choicePlanIndex>=choicePlan.size() || *chosen!=choicePlan[choicePlanIndex])
          throw FormatError("capture controller committed a different choice");
        events << "{\"kind\":\"planned_choice_committed\",\"now\":" << now << ",\"script\":" << step.Script
          << ",\"command\":" << step.Command << ",\"parent\":" << (*chosen)[0] << ",\"child\":" << (*chosen)[1] << "}\n";
        ++choicePlanIndex;
      }
      if(captureParticles && particleCaptureRequested)captureActive=true;
      if(captureResult && !captureChapter && !captureProfile && step.Opcode==0x3f8)captureActive=true;
      if(captureChapter && step.Opcode==0x3ec)captureActive=true;
      if(captureProfile && !captureLip && !captureBattle && !captureVersus && step.Opcode==0x3ed)captureActive=true;
      if(captureLip && step.Opcode==0x2c6)captureActive=true;
      if(captureBattle && step.Opcode==0x3ef)captureActive=true;
      if(captureVersus && step.Opcode==0x3f9)captureActive=true;
      if(captureVersus && step.Opcode==0x3f9 && step.Boundary==VmBoundary::CommandCompletedBeforeDispatch) {
        const auto& v=vm->State().BattleVersus;
        std::ofstream result(captureDirectory/"battle-versus.json");
        result << "{\"script\":" << step.Script << ",\"command\":" << step.Command << ",\"now\":" << now
               << ",\"phase\":" << v.Phase << ",\"completed\":" << v.Completed << ",\"visible\":" << v.Visible
               << ",\"confirmation_sent\":" << versusConfirmSent << ",\"display\":" << vm->State().Battle.Display
               << ",\"battle_atlas_root\":16,\"gpu_atlas_uploads_checked\":" << versusUploadsChecked << ",\"actors\":[";
        for(size_t i=0;i<vm->State().Live2d.Records.size();++i){if(i)result << ',';const auto& r=vm->State().Live2d.Records[i];
          result << '[' << r.LogicalId << ',' << (*r.Position)[0] << ',' << (*r.Position)[1] << ']';}
        result << "],\"right_motion_corrected\":true,\"whole_battle_playability_claimed\":false}\n";
        targetCompleted=true;quit=true;break;
      }
      if(captureBattle && step.Opcode==0x3ef && step.Boundary==VmBoundary::CommandCompletedBeforeDispatch) {
        const auto& state=vm->State();std::ofstream out(captureDirectory/"battle-entry.json");
        out << "{\"script\":" << step.Script << ",\"command\":" << step.Command << ",\"now\":" << now
            << ",\"enabled\":" << state.BattleEntry.Enabled << ",\"sides\":[" << state.Battle.SideCharacters[0] << ',' << state.Battle.SideCharacters[1]
            << "],\"gauge\":" << state.BattleGauge << ",\"display\":" << state.Battle.Display << ",\"history\":" << ExactMusicStatus(state.BattleHistory,size_t(state.BattleEntry.Request->History))
            << ",\"selection_counts\":[" << state.BattleEntry.SelectLoadedCount << ',' << state.BattleEntry.SelectStartedCount
            << "],\"textures\":[" << battleAssets.Textures()[0].size() << ',' << battleAssets.Textures()[1].size()
            << "],\"gpu_uploads_checked\":" << battleUploadsChecked << ",\"idle\":" << battleAssets.Status().Idle()
            << ",\"battle_hud_pixels_claimed\":false}\n";
        targetCompleted=true;quit=true;break;
      }
      if(capture && captureActive && step.Opcode==0x2c6 && step.Boundary==VmBoundary::CommandStarted) {
        const auto& control=*vm->State().MouthControl;
        events << "{\"kind\":\"lip_action\",\"now\":" << now << ",\"script\":" << step.Script << ",\"command\":" << step.Command
            << ",\"enabled\":" << control.OverrideEnabled << ",\"ids\":[";
        for(size_t i=0;i<5;++i){if(i)events << ',';events << control.OverrideIds[i];}events << "]}\n";
      }
      if(step.Boundary==VmBoundary::ProfileDestinationLoaded) {
        profileReturned=true;profileActive=false;
        if(capture && captureActive)events << "{\"kind\":\"profile_destination\",\"now\":" << now << ",\"script\":" << step.Script << ",\"command\":" << step.Command << "}\n";
      }
      if(capture && captureActive && step.Opcode==0x3ec) {
        const auto* chapter=vm->CurrentChapter();
        if(chapter && chapter->Started)events << "{\"kind\":\"chapter_state\",\"now\":" << now
          << ",\"script\":" << step.Script << ",\"command\":" << step.Command << ",\"boundary\":" << int(step.Boundary)
          << ",\"advance\":" << acceptedInput << ",\"stage\":" << chapter->Stage
          << ",\"pending\":" << (chapter->PendingStage?int32_t(*chapter->PendingStage):-1)
          << ",\"visible\":" << vm->State().TitleDisplay.value_or(false) << ",\"id\":" << vm->State().ChapterId
          << ",\"fade\":" << vm->State().ScreenFade.value_or(-1) << ",\"hold\":" << chapter->Hold.Current
          << ",\"done\":" << chapter->Completed << "}\n";
      }
      if(capture && captureActive && step.Opcode==0x3f8) {
        const auto& r=vm->State().ApproachResult;
        events << "{\"kind\":\"approach_result_state\",\"now\":" << now << ",\"script\":" << step.Script
          << ",\"command\":" << step.Command << ",\"boundary\":" << int(step.Boundary)
          << ",\"accept\":" << acceptedInput << ",\"phase\":" << r.Phase << ",\"pending\":" << r.PendingPhase.value_or(-1)
          << ",\"visible\":" << r.Visible << ",\"cursor\":" << r.Cursor << ",\"done\":" << r.Completed
          << ",\"outcome\":" << r.Outcome << ",\"gauge\":" << vm->State().ApproachGauge
          << ",\"voice\":" << resultHost.Text().Voice << ",\"voice_playing\":" << playback.MissionVoicePlaying()
          << ",\"voice_position_ms\":" << playback.MissionVoicePosition()
          << ",\"revealed\":" << resultHost.Text().Reveal.Revealed
          << ",\"screen_fade\":" << vm->State().ScreenFade.value_or(-1)
          << ",\"destination\":" << vm->State().PendingCommandIndex << ",\"auto\":" << skipAuto.Auto
          << ",\"mode\":" << vm->State().SkipMode << ",\"skip_phase\":" << vm->State().SkipPhase << ",\"tweens\":[";
        for(size_t i=0;i<7;++i){if(i)events << ',';events << r.Tweens[i].Current;}events << "]}\n";
      }
      if (!captureConfirmInput && captureFromCommand && step.Script == captureScript &&
          size_t(step.Command) == *captureFromCommand) captureActive = true;
      input.Advance = false;
      input.Approach={};
      syncMouths();
      if (capture && captureActive && (step.Boundary == VmBoundary::CommandStarted ||
                      step.Boundary == VmBoundary::CommandCompletedBeforeDispatch))
        events << "{\"kind\":\"command\",\"frame\":" << frame << ",\"now\":" << now
               << ",\"script\":" << step.Script << ",\"command\":" << step.Command
               << ",\"opcode\":" << step.Opcode << ",\"boundary\":" << int(step.Boundary) << "}\n";
      if (step.Opcode == 0x64 && step.Boundary == VmBoundary::CommandStarted) {
        firstMessage = now;
        laidOut = false;
        capturePointerStage = 0;
        if((captureResult && !captureChapter && !captureProfile && resultCompleted) || (captureChapter && chapterCompleted) ||
            (captureProfile && !captureLip && !captureBattle && !captureVersus && profileReturned)){targetCompleted=true;quit=true;}
        break;
      }
      if(captureResult && step.Opcode==0x3f8 && step.Boundary==VmBoundary::CommandCompletedBeforeDispatch)
        resultCompleted=true;
      if(captureChapter && step.Opcode==0x3ec && step.Boundary==VmBoundary::CommandCompletedBeforeDispatch)
        chapterCompleted=true;
      if(captureLip && captureActive && step.Opcode==0x64 && step.Boundary==VmBoundary::CommandCompletedBeforeDispatch &&
          backend.CurrentMessage() && backend.CurrentMessage()->VoiceId>=0){targetCompleted=true;quit=true;}
      if (step.Boundary == VmBoundary::CommandCompletedBeforeDispatch && capture && !captureParticles && !captureResult &&
          (captureUntilMessage
               ? step.Script == captureScript && size_t(step.Command) == *captureUntilMessage
               : step.Opcode == 0x64 && (!captureVoiced || step.Command >= 67))) {
        targetCompleted = true;
        quit = true;
        break;
      }
      if (step.Boundary == VmBoundary::WaitingInput ||
          step.Boundary == VmBoundary::WaitingBackend) {
        if(captureMenuInput && !menuMissionSent && step.Opcode==0x3f7 && step.Boundary==VmBoundary::WaitingInput)
          menuMissionDue=true;
        if(captureResult && !resultConfirmationSent && step.Opcode==0x3f8 && step.Boundary==VmBoundary::WaitingInput)
          resultConfirmationDue=true;
        if(capture && (captureWaitingInput || captureConfirmInput) && captureUntilMessage &&
            step.Script==captureScript && size_t(step.Command)==*captureUntilMessage &&
            step.Boundary==VmBoundary::WaitingInput) {
          targetWaitingReached=true;
          if(captureWaitingInput){targetCompleted=true;quit=true;}
          else if(!confirmSent){captureActive=true;confirmDue=true;}
        }
        break;
      }
      if (step.Boundary == VmBoundary::Ended) {
        quit = true;
        break;
      }
      if (work == 255)
        throw UnsupportedCommand("scene per-frame command budget exceeded");
    }
    if(capture && captureActive)for(const auto& requestLog:particleRequests) {
      events << "{\"kind\":\"particle_request\",\"now\":" << now << ",\"id\":" << requestLog.Id
        << ",\"seed\":" << requestLog.Seed << ",\"draws_before\":" << requestLog.Before << ",\"words\":["
        << requestLog.Request.StartOffset << ',' << requestLog.Request.Group;
      for(const auto value:requestLog.Request.Owner)events << ',' << std::bit_cast<uint32_t>(value);
      events << "]}\n";
    }
    particleRequests.clear();
    if(const auto* mode=vm->CurrentProfile()) {
      if(!profileActive){profilePresenter.Begin();profileActive=true;}
      profilePresenter.AfterStep(*mode,now);
    }
    if(!vm->CurrentProfile())particles.Update();  // Native749fe: scene manager owns this update.
    for (const auto& request : vm->TakeEvents()) {
      if(request.Kind==GameEventKind::Trophy) {
        if(request.Id<0)throw FormatError("invalid trophy request");
        if(sessionTrophies.insert(request.Id).second)
          SDL_Log("Saekano session trophy %d",request.Id);
      } else if (!backend.ConsumeVoiceEvent(request)) {
        try{playback.Submit(request);}catch(const std::exception& error) {
          throw UnsupportedCommand("audio at dispatch script " + std::to_string(vm->State().ScriptFileId) +
              " command " + std::to_string(vm->State().CurrentCommandIndex) + " time " + std::to_string(now) + ": " + error.what());
        }
      }
      if (capture && captureActive)
        events << "{\"kind\":\"audio\",\"frame\":" << frame
               << ",\"now\":" << now << ",\"event\":" << int(request.Kind)
               << ",\"id\":" << request.Id << "}\n";
    }
    if(const auto* mode=vm->CurrentProfile()) {
      if(!capture || captureActive) {
        int w=0,h=0;SdlCheck(SDL_GetWindowSizeInPixels(window.Window,&w,&h),"profile scene viewport");
        profilePresenter.Draw(*mode,vm->State(),w,h);
        const auto signature=profilePresenter.Signature(*mode);
        if(captureProfile && !captureLip && !captureBattle && !captureVersus && (signature!=profileSignature || now-profileLastSaved>=2000)) {
          const auto file="profile-"+std::to_string(profileSaved++)+".bmp";
          Capture(captureDirectory/file,w,h);profilePresenter.Write(profileFrames,*mode,vm->State(),now,file);
          profileSignature=signature;profileLastSaved=now;
        }
        if(capture && captureActive)events << "{\"kind\":\"profile_state\",\"now\":" << now
          << ",\"mode_id\":" << vm->State().OtakuMode << ",\"phase\":" << mode->Phase()
          << ",\"ordinal\":" << mode->Selection().Ordinal << ",\"selector\":" << mode->Selection().Selectors.at(mode->Selection().Ordinal)
          << ",\"result_revealed\":" << profileServices.ResultText().Revealed << ",\"response_revealed\":" << profileServices.ResponseText().Revealed
          << ",\"voice_stopped\":" << profileVoice.Stopped() << "}\n";
        SdlCheck(SDL_GL_SwapWindow(window.Window),"profile scene swap");
      }
      if(!capture)SDL_Delay(16);
      continue;
    }
    if(captureVersus && targetCompleted)break; // Stop before the still-unbound selection HUD.
    if(captureBattle && captureActive) { // Resource/GPU diagnostic omits the unbound battle HUD.
      if(targetCompleted)break;
      continue;
    }
    const auto spriteClock = capture ? uint32_t(now) : uint32_t(SDL_GetTicks());
    if (capture && captureActive)
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
    models.Frame(vm->State().Live2d, now);
    for (const auto& [id, actor] : vm->State().Characters)
      if (actor.Pose.Ready && actor.Tweens[size_t(CharacterTween::Opacity)].Current != 0)
        mouths.at(id).Update(now);
    backend.UpdateMessage(now);
    const auto& state = vm->State();
    // Run the actual preceding commands/audio to obtain their owning state,
    // but do not render or capture the previously checked scene range again.
    if(capture && !captureActive)continue;
    if (capture && captureActive) {
      const auto* text = backend.CurrentMessage();
      if (text && !text->Parsed.QueueMarkers.empty() && text->Reveal.Started &&
          state.CurrentCommandIndex == int32_t(text->Command)) {
        events << "{\"kind\":\"inline_reveal\",\"now\":" << now << ",\"command\":" << text->Command
               << ",\"voice_position\":" << text->AudioPosition << ",\"epoch\":" << text->Epoch
               << ",\"segment\":" << text->Reveal.SegmentIndex << ",\"revealed\":"
               << (text->Reveal.Revealed ? "true" : "false") << ",\"glyphs\":[";
        for (size_t i = 0; i < text->Reveal.Glyphs.size(); ++i) {
          if (i) events << ','; const auto& glyph = text->Reveal.Glyphs[i];
          events << '[' << int(glyph.Enabled) << ',' << glyph.Alpha.Current << ']';
        }
        events << "]}\n";
      }
    }
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
    const auto* activeMessage = backend.CurrentMessage();
    const bool approach=state.ApproachMode.Enabled.value_or(false);
    if (!activeMessage && !approach && !state.BattleVersus.Visible && !state.TitleDisplay.value_or(false)) {
      UiSpriteBlit white;
      white.Destination = {0, 0, 960, 544};
      white.Color = {255, 255, 255, 255};
      renderer.DrawQuad(white, {0, 1, 1}, true);
      SdlCheck(SDL_GL_SwapWindow(window.Window), "scene prelude swap");
      if (!capture) SDL_Delay(16);
      continue;
    }
    // Empty text contributes no glyphs. A message-free approach frame still
    // draws its scene queue and HUD; report its absent message explicitly.
    const SceneMessageTextState emptyMessage;
    const auto* message=activeMessage?activeMessage:&emptyMessage;
    if (!state.ScreenFade)
      throw UnsupportedCommand("scene screen-fade state is unknown");
    if (!state.Quake[0] || !state.Quake[1] || !state.Quake[2])
      throw UnsupportedCommand("scene quake inputs are missing");
    if (activeMessage && (!laidOut || layoutCommand != message->Command)) {
      layout =
          LayoutPlainText(message->Reveal.Text, format, font.Cached(), metrics,
                          visual.Window.TextWidth, visual.BodyScale,
                          uint32_t(utility.Size), font.NominalSize());
      laidOut = true;
      layoutCommand = message->Command;
    }
    SceneQueueInputs gates;
    gates.ApproachSelectionVisible=state.ApproachSelection.Visible;
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
      } else if (pass.Kind == SceneDrawKind::Live2d) {
        DrawSceneModels(models,state.Live2d,modelTextures,capture);
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
      } else if(pass.Kind==SceneDrawKind::ApproachSelection) {
        particleFragment.SetBlendMode(1);
        renderer.SetFilter(0,0);glUniform1i(renderer.AlphaMode,1);
        const auto draws=BuildApproachSelectionDraws(selectionLayout,selectionShared,*backend.SelectionSettings,
            state.ApproachSelection,vm->CurrentApproachEntries(),state.ApproachFlags,{},utility.Size,
            [&](const auto& text,int32_t size){return int32_t(MeasurePvfText(font.Cached(),text,uint32_t(size),font.NominalSize()));});
        for(const auto& draw:draws) {
          if(draw.Kind==ApproachSelectionDrawKind::Sprite) {
            uploadMain(draw.Sprite.Texture.Entry);drawSprite(draw.Sprite);spriteDraws.push_back(draw.Sprite);
          } else if(draw.Kind==ApproachSelectionDrawKind::Text) {
            fontTextures.DrawString(renderer,font,draw.Text,draw.Position[0],draw.Position[1],uint32_t(draw.FontSize),draw.Color);
            textDraws.push_back({draw.Text,draw.Position,uint32_t(draw.FontSize),draw.Color});
          } else if(draw.Kind==ApproachSelectionDrawKind::ClipBegin) {
            particleFragment.SetAlphaTest(true,6,0);
            const auto& r=draw.Clip;const int x=std::max(0,r[0]),y=std::max(0,r[1]);
            const int right=std::clamp(r[0]+r[2],0,960),bottom=std::clamp(r[1]+r[3],0,544);
            glEnable(GL_SCISSOR_TEST);glScissor(x*width/960,(544-bottom)*height/544,
                std::max(0,right-x)*width/960,std::max(0,bottom-y)*height/544);
          } else {glDisable(GL_SCISSOR_TEST);particleFragment.SetAlphaTest(false,3,0);}
        }
        renderer.SetFilter(*state.ScreenFilter.Mode,*state.ScreenFilter.Intensity);
      } else if (pass.Kind == SceneDrawKind::MessageWindow) {
        OrdinaryMessageWindowState windowState;
        windowState.Openness = state.WindowOpenness[0];
        windowState.OpacitySetting = opacity;
        windowState.Quake = *state.Quake[2];
        windowState.SpeakerId = state.SpeakerId;
        windowState.MessageCommand = activeMessage && state.CurrentCommandIndex==int32_t(message->Command);
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
            if(draw.Index==0)particleFragment.SetAlphaTest(false,3,0); // Native common widget631b4.
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
        particleFragment.SetBlendMode(1);  // Native GpFont draw.
        if(!activeMessage)continue;
        // This entry is an explicitly fresh owner with no loaded read history.
        textDraws = BuildOrdinaryMessageTextDraws(
            visual, message->Parsed, message->Reveal, layout, format,
            font.Cached(), uint32_t(utility.Size), font.NominalSize(),
            state.WindowOpenness[0], *state.Quake[2], visual.UnreadColor);
        for (const auto& text : textDraws)
          fontTextures.DrawString(renderer, font, text.Text, text.Position[0],
                                  text.Position[1], text.Size, text.Color);
      } else if (pass.Kind == SceneDrawKind::Fill) {
        const auto sprite = BuildSceneFill(state.SceneImages.Slots.FillRects.at(pass.Index));
        glUniform1i(renderer.AlphaMode, 1);
        renderer.DrawQuad(sprite, {0, 1, 1}, true);
        spriteDraws.push_back(sprite);
      }
    }
    renderer.SetFilter(0, 0.0f);  // Native resets after queued callbacks, before later HUD.
    if(state.TitleDisplay.value_or(false)) {
      for(const auto& text:BuildChapterTitleText(chapterVisual,state.Titles,uint32_t(utility.Size))) {
        fontTextures.DrawString(renderer,font,text.Text,text.Position[0],text.Position[1],text.Size,text.Color);
        textDraws.push_back(text);
      }
      const auto banner=BuildChapterTitleBanner(chapterVisual,state.ChapterId);
      uploadMain(banner.Texture.Entry);drawSprite(banner);spriteDraws.push_back(banner);
    }
    if(approach) {
      if(!state.ApproachMode.Id || !state.Approach.Known)
        throw UnsupportedCommand("approach HUD owning inputs are unknown");
      for(const auto& sprite:BuildApproachGaugeSprites(approachVisual,state.ApproachGauge,false)) {
        uploadMain(sprite.Texture.Entry);drawSprite(sprite);spriteDraws.push_back(sprite);
      }
      const auto camera=UpdateSceneCamera(particleCamera);ParticleProjectionUniforms projection;
      projection.View=camera.View;projection.Projection=camera.Projection;
      projection.World=ApproachParticleWorld();projection.Aspect=particleCamera.Aspect;
      const auto count=particleRenderer.DrawGroup(particles,6,1.0f,projection,particleFragment,ParticleRasterViewport{});
      if(capture && captureActive && count)events << "{\"kind\":\"particle_group\",\"frame\":" << frame
        << ",\"now\":" << now << ",\"draws\":" << count << ",\"rng_draws\":" << particles.Random().Draws()
        << ",\"blend\":" << particleFragment.BlendIndex << ",\"alpha\":" << particleFragment.AlphaFamily << "}\n";
      if(captureParticles && count && ++particleCaptureFrames>=8){targetCompleted=true;quit=true;}
      for(const auto& sprite:BuildApproachMissionSprites(approachVisual,*state.ApproachMode.Id,state.ApproachMission)) {
        uploadMain(sprite.Texture.Entry);drawSprite(sprite);spriteDraws.push_back(sprite);
      }
      const auto& result=state.ApproachResult;
      const auto layers=BuildApproachResultLayers(resultVisual,result,*state.ApproachMode.Id,960,544);
      for(const auto& command:layers)if(command.Kind==UiRenderKind::Sprite) {
        const auto key=command.Sprite.Texture;
        if(key.Section==ProfileTextureSection::MainUi)uploadMain(key.Entry);
        else if(key.Section==ProfileTextureSection::Character) {
          if(approachAssets.Id()!=state.ApproachMode.Id)
            throw UnsupportedCommand("result character bank does not match the loaded approach resource");
          upload(key,approachAssets.Textures().at(key.Entry).Pixels);
        } else throw UnsupportedCommand("unbound approach result texture bank");
        spriteDraws.push_back(command.Sprite);
      }
      // Preserve native logical state separately from the physical UI pipeline.
      for(const auto& command:layers)if(command.Kind==UiRenderKind::AlphaTest)
        particleFragment.SetAlphaTest(command.TestEnabled.value_or(true),command.Mode,0);
      renderer.Execute(layers);
      if(result.Visible) {
        const auto draws=BuildApproachResultTextDraws(resultHost.Text(),format,resultVisual.TextPosition,uint32_t(utility.Size));
        for(const auto& text:draws) {
          fontTextures.DrawString(renderer,font,text.Text,text.Position[0],text.Position[1],text.Size,text.Color);
          textDraws.push_back(text);
        }
        for(const auto& sprite:BuildApproachResultOverlays(resultVisual,resultCursors,result,messageCursor.Current)) {
          uploadMain(sprite.Texture.Entry);drawSprite(sprite);spriteDraws.push_back(sprite);
        }
      }
    }
    for(const auto& d:BuildBattleVersusDraws(versusVisual,state.BattleVersus,state.Battle.SideCharacters,
        uint32_t(utility.Size),[&](auto text,int32_t size){return int32_t(MeasurePvfText(font.Cached(),text,uint32_t(size),font.NominalSize()));})) {
      if(d.IsText){fontTextures.DrawString(renderer,font,d.Text.Text,d.Text.Position[0],d.Text.Position[1],d.Text.Size,d.Text.Color);textDraws.push_back(d.Text);}
      else {uploadBattleUi(d.Sprite.Texture.Entry);drawSprite(d.Sprite);spriteDraws.push_back(d.Sprite);}
    }
    // Native HUD tail8007708c draws white after queued presentation/HUD.
    if (const auto fade = BuildScreenFade(*state.ScreenFade)) {
      glUniform1i(renderer.AlphaMode, 1);
      renderer.DrawQuad(*fade, {0, 1, 1}, true);
      spriteDraws.push_back(*fade);
    }
    GlCheck("first scene composition");
    std::string signature = std::to_string(message->Reveal.Revealed) + ':' +
                            std::to_string(backend.CursorVisible()) + ':' +
                            std::to_string(pointerDown) + ':' +
                            std::to_string(skipAuto.Auto) + ':' + std::to_string(autoKeyHeld);
    if (captureVoiced) signature = std::to_string(message->Command) + ':' +
        std::to_string(message->Reveal.Revealed) + ':' + std::to_string(message->VoiceGate.Phase);
    signature += ':' + std::to_string(state.RuleWipe.Active) + ':' +
        std::to_string(state.RuleWipe.Auxiliary) + ':' + std::to_string(state.RuleWipe.Progress.value_or(0) / 25);
    signature += ':' + std::to_string(*state.ScreenFilter.Mode) + ':' +
        std::to_string(int32_t(std::clamp(*state.ScreenFilter.Intensity, 0.0f, 1.0f) * 16));
    signature += ':' + std::to_string(*state.ScreenFade / 16);
    if(approach) {
      signature+=':'+std::to_string(state.ApproachMission.Stage);
      for(const auto& tween:state.ApproachMission.Tweens)signature+=':'+std::to_string(tween.Current/16);
      const auto& menu=state.ApproachSelection;
      signature+=':'+std::to_string(menu.Visible)+':'+std::to_string(menu.Phase)+':'+std::to_string(menu.Parent)+':'+std::to_string(menu.Child);
      signature+=':'+std::to_string(menu.Scroll.Current);
      if(captureBranch)for(const auto& record:state.Live2d.Records)
        if(record.ResourceId)signature+=':'+std::to_string(models.Observe(*record.ResourceId).MouthActive);
    }
    if(captureResult && !captureLip) {
      const auto& result=state.ApproachResult;
      signature=std::to_string(result.Visible)+':'+std::to_string(result.Phase)+':'+
          std::to_string(result.PendingPhase.value_or(-1))+':'+std::to_string(result.Cursor)+':'+
          std::to_string(resultHost.Text().Reveal.Revealed)+':'+std::to_string(result.Completed)+':'+std::to_string(quit);
    }
    if(captureChapter) {
      const auto* chapter=vm->CurrentChapter();
      signature=std::to_string(state.TitleDisplay.value_or(false))+':'+std::to_string(chapter?chapter->Stage:99)+':'+
          std::to_string(state.ScreenFade.value_or(-1)/64)+':'+std::to_string(quit);
    }
    if(captureLip)for(const auto& [id,mouth]:mouths)signature+=':'+std::to_string(id)+':'+std::to_string(mouth.Frame);
    if(captureVersus) {
      const auto& v=state.BattleVersus;
      signature=std::to_string(v.Phase)+':'+std::to_string(v.PendingPhase.value_or(-1));
      for(const auto& t:v.Tweens)signature+=':'+std::to_string(t.Current/64);
    }
    if(!captureResult)for (const auto& fill : state.SceneImages.Slots.FillRects)
      signature += ':' + std::to_string(fill.Opacity / 16);
    if(!captureResult)for (const auto& [id, actor] : state.Characters) {
      if (!captureVoiced) signature += ':' + std::to_string(vm->SpriteBlink(id)->Phase);
      if (captureUntilMessage) signature += ':' + std::to_string(actor.Pose.Current) + ':' +
          std::to_string(actor.Tweens[size_t(CharacterTween::PreviousPose)].Current / 64);
    }
    if (capture && captureActive && (saved == 0 || signature != previousSignature ||
                    now - lastSaved >= 1500 || quit)) {
      const auto file = "scene-" + std::to_string(saved++) + ".bmp";
      Capture(captureDirectory / file, width, height);
      report << "{\"file\":\"" << file << "\",\"now\":" << now
             << ",\"battle_versus\":[" << state.BattleVersus.Visible << ',' << state.BattleVersus.Phase << ',' << state.BattleVersus.PendingPhase.value_or(-1) << ']'
             << ",\"script\":" << state.ScriptFileId
             << ",\"command\":" << state.CurrentCommandIndex << ",\"revealed\":"
             << (message->Reveal.Revealed ? "true" : "false")
             << ",\"frame\":" << frame
             << ",\"message_command\":" << message->Command
             << ",\"message_present\":" << (activeMessage?"true":"false")
             << ",\"approach_enabled\":" << (approach?"true":"false")
             << ",\"approach_mission\":[" << state.ApproachMission.Visible << ',' << state.ApproachMission.Stage
             << ',' << state.ApproachMission.PendingStage.value_or(-1) << ',' << state.ApproachMission.Mission << ']'
             << ",\"mission_voice_position_ms\":" << playback.MissionVoicePosition()
             << ",\"pcm_epoch_ms\":";
      if (pcmEpoch) report << *pcmEpoch; else report << "null";
      report
             << ",\"completed\":" << (quit ? "true" : "false")
             << ",\"capture_confirmation_sent\":" << (confirmSent?"true":"false")
             << ",\"capture_wait_reached\":" << (targetWaitingReached?"true":"false")
             << ",\"mission_voice_playing\":" << (playback.MissionVoicePlaying()?"true":"false")
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
                "groups\",\"other_particle_groups\"],\"auto_state\":[" << skipAuto.Auto << ','
             << autoKeyHeld << ',' << autoRotation.Channel.Current << ','
             << autoRotation.Channel.Epoch << ',' << autoRotation.Channel.Sample
             << "],\"message_epoch\":" << message->Epoch
             << ",\"message_source_units\":" << message->SourceUnits
             << ",\"capture_voiced\":" << (captureVoiced ? "true" : "false")
             << ",\"voice_id\":" << message->VoiceId << ",\"voice_phase\":" << message->VoiceGate.Phase
             << ",\"voice_slot\":" << message->VoiceGate.Slot << ",\"voice_position_ms\":" << message->AudioPosition
             << ",\"skip_state\":[" << state.SkipMode << ',' << state.SkipPhase << ']'
             << ",\"history_gate_supplied\":false"
             << ",\"approach_selection\":{\"visible\":" << state.ApproachSelection.Visible
             << ",\"phase\":" << state.ApproachSelection.Phase << ",\"pending\":" << state.ApproachSelection.PendingPhase.value_or(-1)
             << ",\"parent\":" << state.ApproachSelection.Parent << ",\"child\":" << state.ApproachSelection.Child
             << ",\"parent_none\":" << state.ApproachSelection.ParentUnselected << ",\"child_none\":" << state.ApproachSelection.ChildUnselected
             << ",\"weight_bits\":" << std::bit_cast<uint32_t>(state.ApproachSelection.ParentWeight)
             << ",\"scroll\":" << state.ApproachSelection.Scroll.Current << ",\"history_bound\":false,\"bucket\":18}"
             << ",\"menu_capture_step\":" << menuCaptureStep << ",\"menu_prior_mission_confirmed\":" << menuMissionSent
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
      report << "],\"scene_models\":[";
      bool firstModel=true;
      for(const auto& record:state.Live2d.Records) {
        if(!record.ResourceId)continue;if(!firstModel)report << ',';firstModel=false;
        const auto id=*record.ResourceId;const auto observation=models.Observe(id);
        report << "{\"resource\":" << id << ",\"enabled\":" << observation.Enabled
               << ",\"color\":[" << int(observation.Color[0]) << ',' << int(observation.Color[1])
               << ',' << int(observation.Color[2]) << ',' << int(observation.Color[3]) << "],\"matrix\":[";
        const auto matrix=models.Matrix(id);
        for(size_t i=0;i<matrix.size();++i){if(i)report << ',';report << matrix[i];}
        report << "],\"textures\":" << modelTextures.at(id)->Handles.size()
               << ",\"mouth_active\":" << observation.MouthActive << ",\"mouth_bits\":" << std::bit_cast<uint32_t>(observation.Mouth)
               << ",\"ui_gl_state_restored\":true}";
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
      report << "],\"keyword_ranges\":[";
      for(size_t i=0;i<message->Parsed.Keywords.size();++i) {
        if(i)report << ',';const auto& k=message->Parsed.Keywords[i];
        report << '[' << k.Begin << ',' << k.End << ',' << k.Id << ',' << state.Bank0.at(size_t(k.Id)) << ']';
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
    if (!quit || firstMessage == UINT64_MAX || ((captureUntilMessage || captureResult) && !targetCompleted) ||
        ((captureWaitingInput || captureConfirmInput) && !targetWaitingReached) ||
        (captureConfirmInput && !confirmSent))
      throw UnsupportedCommand("scene preview budget exhausted");
  }
  return 0;
}
}  // namespace

int RunSceneSdkUiProbe(const std::filesystem::path& usrdir,
                       const std::filesystem::path& captureDirectory) {
  bool created=false;
  try {
    const auto out=std::filesystem::weakly_canonical(captureDirectory);
    const auto data=std::filesystem::weakly_canonical(usrdir);
    for(auto p=out;!p.empty();p=p.parent_path()) {
      if(p==data || p==data.parent_path())throw FormatError("SDK/UI output overlaps original data");
      if(p==p.parent_path())break;
    }
    if(std::filesystem::exists(out))throw FormatError("choose a fresh SDK/UI capture directory");
    std::filesystem::create_directories(out);created=true;
    GameResources resources(SceneRead(usrdir/"resource.dat"));
    ViewerWindow window(true,true);UiBlitRenderer renderer;
    std::map<int32_t,std::unique_ptr<ModelTextures>> textures;
    SdkLive2dDriver models(resources,Live2dEnvironment::Decode(SceneRead(usrdir/"env/env_live2d.dat")));
    const auto state=RunModelSetupScene(resources,models,{1,13,20});
    models.Frame(state.Live2d,0);models.Frame(state.Live2d,16);
    renderer.Draw({},960,544);DrawSceneModels(models,state.Live2d,textures,true);
    std::vector<uint8_t> pixels(960*544*4);glReadPixels(0,0,960,544,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    size_t visible=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i] || pixels[i+1] || pixels[i+2])++visible;
    if(visible<1000)throw FormatError("SDK/UI probe did not draw visible model pixels");
    const auto raw=SceneRead(usrdir/"env/env_approach.dat");
    const auto visual=ApproachVisualEnvironment::Decode(raw);
    ApproachMissionState mission;const auto environment=ApproachMissionEnvironment::Decode(raw);
    StartApproachMission(mission,environment,10579,0);
    for(const auto now:{400u,416u,912u,928u})UpdateApproachMission(mission,environment,false,now);
    size_t sprites=0;
    for(const auto& list:{BuildApproachGaugeSprites(visual,0,false),BuildApproachMissionSprites(visual,0,mission)})
      for(const auto& sprite:list) {
        if(!renderer.Textures.contains(sprite.Texture))
          renderer.Upload(sprite.Texture,DecodeSceneImage(resources.LoadProfileTexture(sprite.Texture).Data));
        renderer.DrawQuad(sprite,renderer.Textures.at(sprite.Texture),false);++sprites;
      }
    UiSpriteBlit marker;marker.Destination={8,8,8,8};marker.Color={21,63,127,255};
    renderer.DrawQuad(marker,{0,1,1},true);
    std::array<uint8_t,4> sample{};glReadPixels(12,544-12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sample.data());
    if(sample[0]!=21 || sample[1]!=63 || sample[2]!=127)
      throw FormatError("UI draw after SDK did not preserve shader/vertex/blend state");
    GlCheck("SDK/UI sentinel readback");Capture(out/"sdk-ui.bmp",960,544);
    std::ofstream report(out/"sdk-ui.json");
    report << "{\"passed\":true,\"setup_script\":1,\"setup_begin\":13,\"setup_end\":20,"
              "\"sdk_model_pixels\":" << visible << ",\"approach_sprites\":" << sprites
           << ",\"ui_gl_state_restored\":true,\"post_sdk_rgb\":[" << int(sample[0]) << ','
           << int(sample[1]) << ',' << int(sample[2]) << "],\"actual_scene_route\":false}\n";
    if(!report)throw FormatError("cannot save SDK/UI probe report");return 0;
  }catch(const std::exception& error) {
    if(created){std::ofstream out(captureDirectory/"error.txt");out << std::string(error.what()).substr(0,2048);}
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano SDK/UI: %s",error.what());return 1;
  }
}
int RunResultRenderStateProbe(const std::filesystem::path& output) {
  try {
    if(std::filesystem::exists(output))throw FormatError("choose fresh result renderer-state output");
    ViewerWindow window(true,true);UiBlitRenderer renderer;std::ofstream report(output);
    if(!report)throw FormatError("cannot create result renderer-state output");
    renderer.Draw({},1000,800);report << "{\"states\":[";bool first=true;
    const auto sample=[&](UiRenderKind kind,int32_t mode,bool enabled) {
      UiRenderCommand c;c.Kind=kind;c.Mode=mode;c.TestEnabled=enabled;
      renderer.Execute(std::span<const UiRenderCommand>(&c,1));
      GLboolean write=GL_FALSE;GLint depth=0,alpha=0;
      glGetBooleanv(GL_DEPTH_WRITEMASK,&write);glGetIntegerv(GL_DEPTH_FUNC,&depth);
      glGetUniformiv(renderer.Program,renderer.AlphaMode,&alpha);GlCheck("result render-state query");
      if(kind==UiRenderKind::DepthTest) {
        if(bool(glIsEnabled(GL_DEPTH_TEST))!=enabled || bool(write)!=enabled ||
            depth!=int(enabled?(mode==0?GL_NEVER:GL_LEQUAL):GL_ALWAYS))
          throw FormatError("result depth state differs from native enable semantics");
      } else if(alpha!=(enabled?mode:0))throw FormatError("result alpha state differs from native enable semantics");
      if(!first)report << ',';first=false;
      report << "{\"kind\":" << int(kind) << ",\"mode\":" << mode << ",\"enabled\":" << enabled
             << ",\"depth_test\":" << bool(glIsEnabled(GL_DEPTH_TEST)) << ",\"depth_write\":" << bool(write)
             << ",\"depth_function\":" << depth << ",\"alpha_mode\":" << alpha << '}';
    };
    sample(UiRenderKind::DepthTest,0,true);sample(UiRenderKind::DepthTest,5,true);
    sample(UiRenderKind::DepthTest,5,false);sample(UiRenderKind::AlphaTest,6,true);
    sample(UiRenderKind::AlphaTest,3,false);report << "],\"passed\":true,\"pixels_checked\":false}\n";
    return 0;
  }catch(const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano result render-state: %s",e.what());return 1;
  }
}
int RunParticleGroupProbe(const std::filesystem::path& usrdir,
    const std::filesystem::path& fixtureDirectory,const std::filesystem::path& output) {
  try {
    if (std::filesystem::exists(output)) throw FormatError("choose a fresh particle group output");
    const auto raw=SceneRead(fixtureDirectory/"group.bin");PayloadReader reader(raw);
    if (reader.U32()!=0x31524750) throw FormatError("invalid particle group fixture");
    const auto count=reader.U32();if(!count || count>4)throw FormatError("particle group case budget");
    ViewerWindow window(true);ParticleGlRenderer renderer;GameResources resources(SceneRead(usrdir/"resource.dat"));
    const auto environment=ParticleEnvironment::Decode(SceneRead(usrdir/"env/env_particle.dat"));
    struct Target {
      GLuint Fbo=0,Color=0;
      ~Target(){if(Fbo)glDeleteFramebuffers(1,&Fbo);if(Color)glDeleteTextures(1,&Color);}
    } target;
    constexpr uint32_t width=192,height=108;
    glGenFramebuffers(1,&target.Fbo);glBindFramebuffer(GL_FRAMEBUFFER,target.Fbo);
    glGenTextures(1,&target.Color);glBindTexture(GL_TEXTURE_2D,target.Color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.Color,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw FormatError("particle group target incomplete");
    std::ofstream report(output);if(!report)throw FormatError("cannot create particle group report");
    report << "{\"cases\":[";
    const auto scalar=[&](){return std::bit_cast<float>(reader.U32());};
    for(uint32_t c=0;c<count;++c) {
      const auto seed=reader.U32(),id=reader.U32(),ticks=reader.U32();const auto opacity=scalar();
      if(ticks>80)throw FormatError("particle group tick budget");
      ParticleManager manager(seed);manager.LoadEffect(resources,environment,id);
      ParticleEffectRequest request;request.Group=6;for(auto& v:request.Owner)v=scalar();
      ParticleProjectionUniforms projection;for(auto* matrix:{&projection.View,&projection.Projection,&projection.World})for(auto& v:*matrix)v=scalar();
      projection.Aspect=scalar();ParticleRasterViewport viewport;viewport.TargetSize={float(width),float(height)};
      ParticleFragmentState fragment;fragment.BlendIndex=1;
      const auto expectedCount=reader.U32();if(expectedCount>300)throw FormatError("particle group reference budget");
      std::vector<ParticleDrawCommand> expected(expectedCount);
      for(auto& draw:expected) {
        draw.TextureIndex=reader.U32();auto& packet=draw.Packet;
        for(auto& v:packet.Size)v=reader.U32();for(auto& v:packet.Source)v=reader.I32();
        for(auto* vector:{&packet.Position,&packet.Angles})for(auto& v:*vector)v=scalar();
        const auto color=reader.U32();for(size_t i=0;i<4;++i)packet.Bgra[i]=uint8_t(color>>(8*i));
        packet.Intensity=scalar();packet.RenderMode=reader.I32();
      }
      if(!manager.Request(id,request))throw FormatError("particle group resource missing");
      for(uint32_t tick=0;tick<ticks;++tick)manager.Update();
      glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
      glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(.125f,.25f,.5f,1.0f);glClear(GL_COLOR_BUFFER_BIT);
      const auto actualCount=renderer.DrawGroup(manager,6,opacity,projection,fragment,viewport);
      if(actualCount!=expectedCount)throw FormatError("particle GPU group count differs from native");
      std::vector<float> actual(width*height*4),reference(actual.size());
      glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,actual.data());glClear(GL_COLOR_BUFFER_BIT);
      for(const auto& draw:expected) {
        const auto& texture=renderer.Texture(manager,draw.TextureIndex);
        renderer.Draw(draw.Packet,texture.Handle,texture.Width,texture.Height,projection,fragment,viewport);
      }
      glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,reference.data());GlCheck("particle group readback");
      size_t changed=0;constexpr std::array<float,4> clear{.125f,.25f,.5f,1};
      for(size_t i=0;i<actual.size();++i) {
        if(!std::isfinite(actual[i]) || std::bit_cast<uint32_t>(actual[i])!=std::bit_cast<uint32_t>(reference[i]))
          throw FormatError("particle group pixels differ from native packet reference");
        changed+=actual[i]!=clear[i%4];
      }
      if(!changed)throw FormatError("particle group produced no changed components");
      if(c)report << ',';report << "{\"effect\":" << id << ",\"ticks\":" << ticks << ",\"draws\":" << actualCount
        << ",\"components\":" << actual.size() << ",\"changed_components\":" << changed << '}';
      renderer.ResetTextures();  // Manager storage can reuse its address next case.
    }
    if(reader.Remaining())throw FormatError("trailing particle group fixture");
    report << "],\"passed\":true,\"native_packet_reference\":true,\"scene_owner_bound\":false}\n";return 0;
  }catch(const std::exception& error){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Particle group probe: %s",error.what());return 1;}
}
int RunParticleRasterProbe(const std::filesystem::path& fixtureDirectory,
                           const std::filesystem::path& output) {
  try {
    if (std::filesystem::exists(output)) throw FormatError("choose a fresh particle raster output");
    const auto bytes=SceneRead(fixtureDirectory/"cases.bin");PayloadReader reader(bytes);
    if (reader.U32()!=0x31525350) throw FormatError("invalid particle raster fixture");
    const auto count=reader.U32();if (!count || count>8) throw FormatError("particle raster case budget");
    const auto sourcePixels=SceneRead(fixtureDirectory/"pixels.bin");
    if (sourcePixels.size()!=64) throw FormatError("particle raster texture fixture size");
    ViewerWindow window(true);ParticleGlRenderer actual;
    struct Target {
      GLuint Framebuffer=0,Color=0,Image=0;
      ~Target() { if (Framebuffer) glDeleteFramebuffers(1,&Framebuffer);
        if (Color) glDeleteTextures(1,&Color);if (Image) glDeleteTextures(1,&Image); }
    } target;
    constexpr uint32_t width=48,height=32;
    glGenTextures(1,&target.Image);glBindTexture(GL_TEXTURE_2D,target.Image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,sourcePixels.data());
    glGenTextures(1,&target.Color);glBindTexture(GL_TEXTURE_2D,target.Color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
    glGenFramebuffers(1,&target.Framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,target.Framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.Color,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
      throw FormatError("particle raster target incomplete");
    struct Reference {
      GLuint Program=0,Vao=0,Buffer=0;
      Reference(const std::string& vertex,const std::string& fragment) {
        const auto vs=UiBlitRenderer::Compile(GL_VERTEX_SHADER,vertex.c_str());
        const auto fs=UiBlitRenderer::Compile(GL_FRAGMENT_SHADER,fragment.c_str());
        Program=glCreateProgram();glAttachShader(Program,vs);glAttachShader(Program,fs);
        glLinkProgram(Program);glDeleteShader(vs);glDeleteShader(fs);
        GLint linked=0;glGetProgramiv(Program,GL_LINK_STATUS,&linked);
        if (!linked) { char log[1024]{};glGetProgramInfoLog(Program,sizeof(log),nullptr,log);throw FormatError(log); }
        glGenVertexArrays(1,&Vao);glGenBuffers(1,&Buffer);
      }
      ~Reference() { if (Program) glDeleteProgram(Program);if (Buffer) glDeleteBuffers(1,&Buffer);if (Vao) glDeleteVertexArrays(1,&Vao); }
    };
    const auto readText=[&](const std::string& name) {
      const auto raw=SceneRead(fixtureDirectory/name);return std::string(raw.begin(),raw.end());
    };
    const auto referenceVertex=readText("reference.vert");
    std::map<uint32_t,std::unique_ptr<Reference>> references;
    std::ofstream report(output);if (!report) throw FormatError("cannot create particle raster report");
    const auto scalar=[&]() { return std::bit_cast<float>(reader.U32()); };
    const auto reset=[&]() {
      glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
      glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
      glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_BLEND);
      glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    };
    const auto save=[&](const std::string& name) {
      if (std::filesystem::exists(fixtureDirectory/name))
        throw FormatError("choose fresh particle raster image outputs");
      std::array<float,width*height*4> pixels;
      glReadPixels(0,0,width,height,GL_RGBA,GL_FLOAT,pixels.data());GlCheck("particle raster readback");
      std::ofstream file(fixtureDirectory/name,std::ios::binary);
      file.write(reinterpret_cast<const char*>(pixels.data()),sizeof(pixels));
      if (!file) throw FormatError("particle raster output write failed");
    };
    for (uint32_t i=0;i<count;++i) {
      ParticleProjectionUniforms projection;
      for (auto* matrix:{&projection.View,&projection.Projection,&projection.World}) for (auto& value:*matrix) value=scalar();
      projection.Aspect=scalar();std::array<float,52> referenceUniforms;
      for (auto& value:referenceUniforms) value=scalar();
      ParticleDrawPacket packet;
      for (auto& value:packet.Size) value=reader.U32();
      for (auto& value:packet.Source) value=reader.I32();
      for (auto* vector:{&packet.Position,&packet.Angles}) for (auto& value:*vector) value=scalar();
      const auto color=reader.U32();for (size_t c=0;c<4;++c) packet.Bgra[c]=uint8_t(color>>(8*c));
      packet.RenderMode=reader.I32();packet.Intensity=scalar();
      ParticleFragmentState fragment;fragment.BlendIndex=reader.U32();fragment.AlphaFamily=reader.U32();fragment.AlphaThreshold=scalar();
      if (fragment.BlendIndex!=0 || fragment.AlphaFamily>6) throw FormatError("unsupported reference raster state");
      std::array<float,80> expanded;for (auto& value:expanded) value=scalar();
      reset();glUseProgram(0);glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
      const auto before=UiGlState();
      actual.Draw(packet,target.Image,4,4,projection,fragment,{});
      if (before!=UiGlState()) throw FormatError("particle raster disturbed UI GL state");
      const auto actualName="actual"+std::to_string(i)+".rgba32f";save(actualName);
      if (!references.contains(fragment.AlphaFamily))
        references.emplace(fragment.AlphaFamily,std::make_unique<Reference>(referenceVertex,
            readText("reference"+std::to_string(fragment.AlphaFamily)+".frag")));
      auto& ref=*references.at(fragment.AlphaFamily);reset();glUseProgram(ref.Program);
      glBindVertexArray(ref.Vao);glBindBuffer(GL_ARRAY_BUFFER,ref.Buffer);
      glBufferData(GL_ARRAY_BUFFER,sizeof(expanded),expanded.data(),GL_STREAM_DRAW);
      for (GLuint attribute=0;attribute<5;++attribute) {
        glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,4,GL_FLOAT,GL_FALSE,
            20*sizeof(float),reinterpret_cast<const void*>(attribute*4*sizeof(float)));
      }
      glUniform4fv(glGetUniformLocation(ref.Program,"referenceUniforms"),13,referenceUniforms.data());
      glUniform4f(glGetUniformLocation(ref.Program,"renderVertInfo.viewport_flip"),1,-1,1,1);
      glUniform1f(glGetUniformLocation(ref.Program,"renderVertInfo.viewport_flag"),1);
      glUniform1f(glGetUniformLocation(ref.Program,"renderVertInfo.screen_width"),960);
      glUniform1f(glGetUniformLocation(ref.Program,"renderVertInfo.screen_height"),544);
      glUniform1f(glGetUniformLocation(ref.Program,"renderVertInfo.z_offset"),.5f);
      glUniform1f(glGetUniformLocation(ref.Program,"renderVertInfo.z_scale"),.5f);
      glUniform1i(glGetUniformLocation(ref.Program,"image"),0);
      glUniform1f(glGetUniformLocation(ref.Program,"nativeAlphaThreshold"),fragment.AlphaThreshold);
      glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,target.Image);
      glDrawArrays(GL_TRIANGLE_STRIP,0,4);
      const auto referenceName="reference"+std::to_string(i)+".rgba32f";save(referenceName);
      report<<"{\"case\":"<<i<<",\"actual\":\""<<actualName<<"\",\"reference\":\""<<referenceName
            <<"\",\"ui_state_restored\":true}\n";
    }
    if (reader.Remaining()) throw FormatError("trailing particle raster fixture");
    return 0;
  } catch (const std::exception& error) {
    std::ofstream failure(output.string()+".error.txt");failure<<std::string(error.what()).substr(0,2048)<<'\n';
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano particle raster: %s",error.what());return 1;
  }
}
int RunParticleProjectionProbe(const std::filesystem::path& fixtureDirectory,
                               const std::filesystem::path& output) {
  try {
    if (std::filesystem::exists(output)) throw FormatError("choose a fresh particle projection output");
    const auto bytes = SceneRead(fixtureDirectory / "cases.bin"); PayloadReader reader(bytes);
    if (reader.U32() != 0x314a5250) throw FormatError("invalid particle projection fixture");
    const auto count = reader.U32(); if (!count || count > 32) throw FormatError("particle projection case budget exceeded");
    const auto referenceBytes = SceneRead(fixtureDirectory / "reference.vert");
    const std::string referenceSource(referenceBytes.begin(), referenceBytes.end());
    ViewerWindow window(true);
    struct Program {
      GLuint Handle = 0, Vao = 0, Input = 0, Feedback = 0;
      Program(const char* source) {
        const auto shader = UiBlitRenderer::Compile(GL_VERTEX_SHADER, source);
        Handle = glCreateProgram(); glAttachShader(Handle, shader);
        const char* outputs[]{"particleClip", "particleColor", "particleUv"};
        glTransformFeedbackVaryings(Handle, 3, outputs, GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(Handle); glDeleteShader(shader);
        GLint linked = 0; glGetProgramiv(Handle, GL_LINK_STATUS, &linked);
        if (!linked) { char log[1024]{}; glGetProgramInfoLog(Handle, sizeof(log), nullptr, log); throw FormatError(log); }
        glGenVertexArrays(1, &Vao); glGenBuffers(1, &Input); glGenBuffers(1, &Feedback);
      }
      ~Program() {
        if (Feedback) glDeleteBuffers(1, &Feedback); if (Input) glDeleteBuffers(1, &Input);
        if (Vao) glDeleteVertexArrays(1, &Vao); if (Handle) glDeleteProgram(Handle);
      }
      std::array<float,10> Sample(const void* vertex, size_t size, bool nativeLayout, const std::array<float,52>& uniforms) {
        glUseProgram(Handle); glBindVertexArray(Vao); glBindBuffer(GL_ARRAY_BUFFER, Input);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(size), vertex, GL_STREAM_DRAW);
        for (GLuint attribute = 0; attribute < 5; ++attribute) {
          glEnableVertexAttribArray(attribute);
          if (nativeLayout) {
            constexpr GLint components[]{3,4,2,3,3}; constexpr size_t offsets[]{0,12,16,24,36};
            glVertexAttribPointer(attribute, components[attribute], attribute == 1 ? GL_UNSIGNED_BYTE : GL_FLOAT,
                                  attribute == 1 ? GL_TRUE : GL_FALSE, sizeof(ParticleVertex),
                                  reinterpret_cast<const void*>(offsets[attribute]));
          } else glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, 20*sizeof(float),
                                       reinterpret_cast<const void*>(size_t(attribute)*4*sizeof(float)));
        }
        const auto location = glGetUniformLocation(Handle, nativeLayout ? "nativeUniforms" : "referenceUniforms");
        if (location < 0) throw FormatError("particle projection uniforms absent");
        glUniform4fv(location, 13, uniforms.data());
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, Feedback); glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 10*sizeof(float), nullptr, GL_STREAM_READ);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, Feedback); glEnable(GL_RASTERIZER_DISCARD);
        glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 1); glEndTransformFeedback();
        glDisable(GL_RASTERIZER_DISCARD); std::array<float,10> result;
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, sizeof(result), result.data());
        GlCheck("particle projection feedback"); return result;
      }
    } actual(ParticleMode0VertexShader), reference(referenceSource.c_str());
    static_assert(offsetof(ParticleVertex,Rgba)==12 && offsetof(ParticleVertex,Uv)==16 &&
                  offsetof(ParticleVertex,Position)==24 && offsetof(ParticleVertex,Angles)==36);
    std::ofstream report(output); if (!report) throw FormatError("cannot create particle projection report");
    const auto scalar = [&]() { return std::bit_cast<float>(reader.U32()); };
    for (uint32_t i = 0; i < count; ++i) {
      ParticleProjectionUniforms uniforms;
      for (auto* matrix : {&uniforms.View,&uniforms.Projection,&uniforms.World}) for (auto& value : *matrix) value = scalar();
      uniforms.Aspect = scalar(); std::array<float,52> referenceUniforms; for (auto& value : referenceUniforms) value = scalar();
      ParticleVertex vertex; for (auto& value : vertex.LocalPosition) value = scalar();
      const auto color = reader.U32(); for (size_t j = 0; j < 4; ++j) vertex.Rgba[j] = uint8_t(color >> (j*8));
      for (auto& value : vertex.Uv) value = scalar();
      for (auto* values : {&vertex.Position,&vertex.Angles}) for (auto& value : *values) value = scalar();
      std::array<float,20> referenceVertex; for (auto& value : referenceVertex) value = scalar();
      const auto obtained = actual.Sample(&vertex,sizeof(vertex),true,uniforms.Pack());
      const auto expected = reference.Sample(referenceVertex.data(),sizeof(referenceVertex),false,referenceUniforms);
      report << "{\"case\":" << i << ",\"actual_bits\":[";
      for (size_t j = 0; j < obtained.size(); ++j) { if (j) report << ','; report << std::bit_cast<uint32_t>(obtained[j]); }
      report << "],\"reference_bits\":[";
      for (size_t j = 0; j < expected.size(); ++j) { if (j) report << ','; report << std::bit_cast<uint32_t>(expected[j]); }
      report << "]}\n";
    }
    if (reader.Remaining()) throw FormatError("trailing particle projection fixture");
    return 0;
  } catch (const std::exception& error) {
    const auto diagnostic = output.string()+".error.txt";
    if (!std::filesystem::exists(diagnostic)) { std::ofstream failure(diagnostic); failure << std::string(error.what()).substr(0,2048) << '\n'; }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Saekano particle projection: %s",error.what()); return 1;
  }
}
int RunParticleFragmentProbe(const std::filesystem::path& fixtureDirectory,
                             const std::filesystem::path& output) {
  try {
    if (std::filesystem::exists(output)) throw FormatError("choose a fresh particle fragment output");
    const auto bytes = SceneRead(fixtureDirectory / "cases.bin");
    PayloadReader reader(bytes);
    const auto magic = reader.U32();
    if (magic != 0x31465050 && magic != 0x32465050) throw FormatError("invalid particle fragment fixture");
    const bool alphaFamilies = magic == 0x32465050;
    const auto count = reader.U32();
    if (!count || count > 32) throw FormatError("particle fragment case budget exceeded");
    reader.CheckCount(count, alphaFamilies ? 44 : 36);
    ViewerWindow window(true);
    std::map<uint32_t, std::unique_ptr<UiBlitRenderer>> actualPrograms;
    std::map<uint32_t, std::unique_ptr<UiBlitRenderer>> references;
    const auto getReference = [&](uint32_t family) -> UiBlitRenderer& {
      if (!references.contains(family)) {
        const auto fragmentBytes = SceneRead(fixtureDirectory / (alphaFamilies
            ? "reference" + std::to_string(family) + ".frag" : "reference.frag"));
        const std::string fragment(fragmentBytes.begin(), fragmentBytes.end());
        references.emplace(family, std::make_unique<UiBlitRenderer>(fragment.c_str()));
      }
      return *references.at(family);
    };
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
      throw FormatError("particle fragment framebuffer is incomplete");
    std::ofstream report(output);
    if (!report) throw FormatError("cannot create particle fragment output");
    for (uint32_t i = 0; i < count; ++i) {
      const auto pixel = reader.U32(), color = reader.U32(), blendIndex = reader.U32();
      std::array<GLenum,4> referenceFactors;
      for (auto& value : referenceFactors) value = reader.U32();
      const bool referenceEnabled = referenceFactors[0] != UINT32_MAX;
      const auto destination = reader.U32();
      const auto alpha = std::bit_cast<float>(reader.U32());
      ParticleFragmentState state;
      state.BlendIndex = blendIndex;
      if (alphaFamilies) {
        state.AlphaFamily = reader.U32();
        state.AlphaThreshold = std::bit_cast<float>(reader.U32());
        if (state.AlphaFamily > 6) throw FormatError("unknown particle alpha family");
      }
      if (!actualPrograms.contains(state.AlphaFamily))
        actualPrograms.emplace(state.AlphaFamily,
            std::make_unique<UiBlitRenderer>(ParticleFragmentShaders[state.AlphaFamily]));
      auto& actual = *actualPrograms.at(state.AlphaFamily);
      auto& reference = getReference(state.AlphaFamily);
      const ProfileTextureKey key{ProfileTextureSection::MainUi, -1, i};
      const ImagePixels pixels{1, 1, {uint8_t(pixel), uint8_t(pixel >> 8), uint8_t(pixel >> 16), uint8_t(pixel >> 24)}};
      UiSpriteBlit draw;
      draw.Texture = key; draw.Destination = {0,0,960,544}; draw.Source = {0,0,1,1};
      draw.Color = {uint8_t(color), uint8_t(color >> 8), uint8_t(color >> 16), uint8_t(color >> 24)};
      const auto sample = [&](UiBlitRenderer& renderer, bool production) {
        renderer.Upload(key, pixels);
        renderer.Draw({}, 960, 544);
        glViewport(0,0,1,1);
        glClearColor(float(destination & 255)/255, float((destination >> 8) & 255)/255,
                     float((destination >> 16) & 255)/255, alpha);
        glClear(GL_COLOR_BUFFER_BIT);
        if (production) ApplyParticleFragmentState(state, renderer.Program);
        else if (referenceEnabled) {
          glEnable(GL_BLEND); glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
          glBlendFuncSeparate(referenceFactors[0], referenceFactors[1], referenceFactors[2], referenceFactors[3]);
        } else glDisable(GL_BLEND);
        if (!production)
          glUniform1f(glGetUniformLocation(renderer.Program, "nativeAlphaThreshold"), state.AlphaThreshold);
        renderer.DrawQuad(draw, renderer.Textures.at(key), false);
        std::array<float,4> result;
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,result.data());
        GlCheck("particle fragment sample");
        return result;
      };
      const auto obtained = sample(actual, true), expected = sample(reference, false);
      report << "{\"case\":" << i << ",\"actual_bits\":[";
      for (size_t c = 0; c < 4; ++c) { if (c) report << ','; report << std::bit_cast<uint32_t>(obtained[c]); }
      report << "],\"reference_bits\":[";
      for (size_t c = 0; c < 4; ++c) { if (c) report << ','; report << std::bit_cast<uint32_t>(expected[c]); }
      report << "]}\n";
    }
    if (reader.Remaining()) throw FormatError("trailing particle fragment fixture");
    return 0;
  } catch (const std::exception& error) {
    const auto diagnostic = output.string() + ".error.txt";
    if (!std::filesystem::exists(diagnostic)) {
      std::ofstream failure(diagnostic);
      failure << std::string(error.what()).substr(0, 2048) << '\n';
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Saekano particle fragment probe: %s", error.what());
    return 1;
  }
}
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
                        std::optional<size_t> captureFromCommand,
                          std::optional<int32_t> captureScriptOverride,bool captureWaitingInput,bool captureConfirmInput,int32_t captureMenuInput,
                          const std::filesystem::path& capturePlan) {
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
    InitializeFreshBattleOwner(owner);
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
    SystemSoundAssets systemSounds(SceneRead(usrdir/"first.dat"));
    MusicPlayback playback(music, capture, &sounds);
    // Fresh standalone viewer has the same native S60235 option8 as profile flow.
    playback.BindUiSounds(systemSounds,ProfileVoiceGain(8,1.0f));
    return RunCompletedScene(window, renderer, fontTextures, font, resources,
                             playback, music, sounds, std::move(owner), mode,
                             usrdir, captureDirectory, captureAuto, captureButtonAuto, captureVoiced,
                             captureUntilMessage, captureFromCommand,captureScriptOverride,captureWaitingInput,captureConfirmInput,captureMenuInput,capturePlan);
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
