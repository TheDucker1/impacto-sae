#include "../scene2d.h"
#include "renderer.h"
#include "../../formaterror.h"
#include "particlerastervertexshader.h"
#include "particlefragmentshader.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace Impacto::OpenGL {
namespace {
void CheckGl(const char* operation) {
  const auto error = glGetError();
  if (error != GL_NO_ERROR)
    throw FormatError(std::string(operation) + ": OpenGL error " +
                      std::to_string(error));
}
GLuint Compile(GLenum type, const char* source) {
  const auto shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048]{};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    glDeleteShader(shader);
    throw FormatError(log);
  }
  return shader;
}
GLuint Link(const char* vertex, const char* fragment) {
  GLuint v = 0, f = 0, p = 0;
  try {
    v = Compile(GL_VERTEX_SHADER, vertex);
    f = Compile(GL_FRAGMENT_SHADER, fragment);
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[2048]{};
      glGetProgramInfoLog(p, sizeof(log), nullptr, log);
      throw FormatError(log);
    }
  } catch (...) {
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    if (p) glDeleteProgram(p);
    throw;
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}
class State final : public SceneRenderState {
  using Push = void(APIENTRY*)(GLbitfield);
  using Pop = void(APIENTRY*)();
  Pop PopAttributes = nullptr, PopClient = nullptr;
  GLint Program = 0, Vao = 0, Array = 0, Active = 0, Read = 0, Draw = 0;
  std::array<GLint, 3> Samplers{};

 public:
  State() {
    const auto push =
        reinterpret_cast<Push>(SDL_GL_GetProcAddress("glPushAttrib"));
    const auto pushClient =
        reinterpret_cast<Push>(SDL_GL_GetProcAddress("glPushClientAttrib"));
    PopAttributes = reinterpret_cast<Pop>(SDL_GL_GetProcAddress("glPopAttrib"));
    PopClient =
        reinterpret_cast<Pop>(SDL_GL_GetProcAddress("glPopClientAttrib"));
    if (!push || !pushClient || !PopAttributes || !PopClient)
      throw FormatError("legacy GL state functions unavailable");
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &Vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &Array);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &Active);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &Read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &Draw);
    for (GLuint i = 0; i < Samplers.size(); ++i)
      glGetIntegeri_v(GL_SAMPLER_BINDING, i, &Samplers[i]);
    push(0x000fffff);
    glBindVertexArray(0);
    pushClient(0xffffffff);
  }
  ~State() {
    GLC::BindFramebuffer(GL_READ_FRAMEBUFFER, Read);
    GLC::BindFramebuffer(GL_DRAW_FRAMEBUFFER, Draw);
    glBindVertexArray(0);
    PopClient();
    PopAttributes();
    glUseProgram(Program);
    glBindVertexArray(Vao);
    glBindBuffer(GL_ARRAY_BUFFER, Array);
    glActiveTexture(Active);
    for (GLuint i = 0; i < Samplers.size(); ++i) glBindSampler(i, Samplers[i]);
    Impacto::Renderer->InvalidateState();
  }
};
struct Target final : SceneRenderTarget {
  GLuint Fbo = 0;
  ~Target() {
    if (Fbo) glDeleteFramebuffers(1, &Fbo);
    if (Texture.Handle) Impacto::Renderer->FreeTexture(Texture.Handle);
  }
};
GLenum Factor(uint8_t factor, bool alpha) {
  switch (factor) {
    case 0:
      return GL_ZERO;
    case 1:
      return GL_ONE;
    case 2:
      return alpha ? GL_SRC_ALPHA : GL_SRC_COLOR;
    case 4:
      return GL_SRC_ALPHA;
    case 5:
      return GL_ONE_MINUS_SRC_ALPHA;
    default:
      throw FormatError("unsupported scene blend factor");
  }
}
const char VertexShader[] = R"GLSL(#version 330
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
out vec2 texturePosition;
void main(){texturePosition=uv;gl_Position=vec4(position.x/480.0-1.0,1.0-position.y/272.0,0.0,1.0);}
)GLSL";
const char FragmentShader[] = R"SCENEFRAG(#version 330
#extension GL_ARB_shading_language_packing : require
#extension GL_ARB_gpu_shader5 : require
in vec2 texturePosition;
uniform sampler2D image;
uniform sampler2D ruleMask;
uniform vec4 tint;
uniform bool solid;
uniform int alphaMode;
uniform int filterMode;
uniform float filterValue;
uniform bool glyph;
uniform bool alphaGlyph;
uniform bool coloredRule;
uniform bool imageRule;
uniform float ruleValue;
out vec4 outputColor;
// Original image-rule GXP arithmetic; preserve its packed register stores.
vec4 pa[32],sa[32],internals[3];
float pack2xF16(vec2 _240)
{
    return uintBitsToFloat(packHalf2x16(_240));
}

void primary_program()
{
    vec2 _162 = vec2(pa[1].x, pa[1].y);
    vec2 _170 = vec2(pa[1].z, pa[1].w);
    internals[0] = vec4(_162.x, _162.y, _170.x, _170.y);
    internals[0] = pa[0] * internals[0];
    vec4 _201 = internals[1];
    _201.x = sa[0].x + pa[2].w;
    internals[1] = _201;
    vec4 _215 = internals[1];
    _215.x = min(internals[1].x, sa[0].z);
    internals[1] = _215;
    vec4 _224 = internals[1];
    _224.x = max(internals[1].x, 0.0);
    internals[1] = _224;
    vec4 _235 = vec4(1.0, 1.0, 1.0, internals[1].x) * internals[0];
    vec2 _251 = vec2(pack2xF16(vec2(_235.x, _235.y)), pack2xF16(vec2(_235.z, _235.w)));
    pa[0] = vec4(_251.x, _251.y, pa[0].z, pa[0].w);
}

vec2 unpack2xF16(float _116)
{
    return unpackHalf2x16(floatBitsToUint(_116));
}

void frag_output_finalize()
{
    vec2 _113 = vec2(pa[0].x, pa[0].y);
    vec2 _122 = unpack2xF16(_113.x);
    vec2 _124 = unpack2xF16(_113.y);
    outputColor = vec4(_122.x, _122.y, _124.x, _124.y);
}

void main(){
  outputColor=solid?tint:texture(image,texturePosition)*tint;
  // Native rectangles and glyphs use independent fragment programs.
  if(alphaGlyph){
    // Original GXP8025729c: the generated A8 atlas supplies coverage only.
    // Multiplying interpolated RGB introduces dark edges on colored text.
    vec4 result=tint*vec4(1.0,1.0,1.0,texture(image,texturePosition).a);
    outputColor=vec4(unpackHalf2x16(packHalf2x16(result.xy)),unpackHalf2x16(packHalf2x16(result.zw)));
  }else if(imageRule){
    sa[0]=vec4(ruleValue,0.0,1.0,0.0);
    pa[0]=tint;pa[1]=texture(image,texturePosition);pa[2]=texture(ruleMask,texturePosition);
    primary_program();frag_output_finalize();
  }else if(coloredRule){
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
)SCENEFRAG";
class Pass final : public Scene2D {
  GLuint Program = 0, Vao = 0, Buffer = 0, ParticleVao = 0, ParticleBuffer = 0;
  std::array<GLuint, 7> ParticlePrograms{};
  void Uniform(const char* name, int value) {
    glUniform1i(glGetUniformLocation(Program, name), value);
  }

 public:
  Pass() {
    try {
      Program = Link(VertexShader, FragmentShader);
      glGenVertexArrays(1, &Vao);
      glGenBuffers(1, &Buffer);
      glBindVertexArray(Vao);
      glBindBuffer(GL_ARRAY_BUFFER, Buffer);
      glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), nullptr,
                   GL_DYNAMIC_DRAW);
      for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(i * 2 * sizeof(float)));
      }
      glGenVertexArrays(1, &ParticleVao);
      glGenBuffers(1, &ParticleBuffer);
      Bind();
    } catch (...) {
      Release();
      throw;
    }
  }
  ~Pass() { Release(); }
  void Release() {
    for (auto p : ParticlePrograms)
      if (p) glDeleteProgram(p);
    if (Program) glDeleteProgram(Program);
    if (Buffer) glDeleteBuffers(1, &Buffer);
    if (Vao) glDeleteVertexArrays(1, &Vao);
    if (ParticleBuffer) glDeleteBuffers(1, &ParticleBuffer);
    if (ParticleVao) glDeleteVertexArrays(1, &ParticleVao);
  }
  void Bind() override {
    glUseProgram(Program);
    glBindVertexArray(Vao);
    glBindBuffer(GL_ARRAY_BUFFER, Buffer);
    for (GLuint i = 0; i < 3; ++i) glBindSampler(i, 0);
    glActiveTexture(GL_TEXTURE0);
    Uniform("image", 0);
    Uniform("ruleMask", 1);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    SetBlend({});
    SetAlphaTest(0);
    SetFilter(0, 0);
  }
  void SetViewport(std::array<int, 4> v) override {
    glViewport(v[0], v[1], v[2], v[3]);
  }
  std::array<int, 4> GetViewport() const override {
    std::array<int, 4> v{};
    glGetIntegerv(GL_VIEWPORT, v.data());
    return v;
  }
  void Clear(std::array<float, 4> c) override {
    glDisable(GL_SCISSOR_TEST);
    glStencilMask(0xff);
    glDepthMask(GL_TRUE);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClearDepth(1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  }
  void SetBlend(SceneBlend b) override {
    if (!b.Enabled) {
      glDisable(GL_BLEND);
      return;
    }
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(
        Factor(b.ColorSource, false), Factor(b.ColorDestination, false),
        Factor(b.AlphaSource, true), Factor(b.AlphaDestination, true));
  }
  void SetFilter(int mode, float value) override {
    if ((mode != 0 && mode != 1) || !std::isfinite(value))
      throw FormatError("invalid scene filter");
    glUseProgram(Program);
    Uniform("filterMode", mode);
    glUniform1f(glGetUniformLocation(Program, "filterValue"),
                std::clamp(value, 0.f, 1.f));
  }
  void SetAlphaTest(int mode) override {
    glUseProgram(Program);
    Uniform("alphaMode", mode);
  }
  void SetDepthTest(int mode, bool enabled) override {
    if (mode != 0 && mode != 5)
      throw FormatError("unsupported scene depth comparison");
    if (enabled) {
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);
      glDepthFunc(mode == 0 ? GL_NEVER : GL_LEQUAL);
    } else {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      glDepthFunc(GL_ALWAYS);
    }
  }
  void SetStencil(int mode) override {
    if (!mode) {
      glDisable(GL_STENCIL_TEST);
      return;
    }
    if (mode < 1 || mode > 3) throw FormatError("unknown scene stencil mode");
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glStencilFunc(mode == 3 ? GL_EQUAL : GL_ALWAYS, mode == 1 ? 1 : 0, 0xff);
    glStencilOp(GL_KEEP, mode == 3 ? GL_KEEP : GL_REPLACE,
                mode == 3 ? GL_KEEP : GL_REPLACE);
  }
  void DrawQuad(const SceneQuad& q, SceneTexture t,
                SceneTexture mask) override {
    glUseProgram(Program);
    glBindVertexArray(Vao);
    glBindBuffer(GL_ARRAY_BUFFER, Buffer);
    Uniform("solid", q.Solid);
    Uniform("glyph", q.Glyph);
    Uniform("alphaGlyph", q.AlphaGlyph);
    Uniform("coloredRule", q.ColoredRule);
    Uniform("imageRule", q.ImageRule);
    glUniform1f(glGetUniformLocation(Program, "ruleValue"), q.RuleValue);
    std::array<float, 24> v{};
    constexpr size_t order[]{0, 1, 2, 2, 1, 3};
    for (size_t i = 0; i < 6; ++i)
      std::copy(q.Vertices[order[i]].begin(), q.Vertices[order[i]].end(),
                v.begin() + i * 4);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v.data());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mask.Handle);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.Handle);
    glUniform4f(glGetUniformLocation(Program, "tint"), q.Color[0] / 255.f,
                q.Color[1] / 255.f, q.Color[2] / 255.f, q.Color[3] / 255.f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    CheckGl("scene quad");
  }
  void SetScissor(std::optional<std::array<int, 4>> rect) override {
    if (!rect) {
      glDisable(GL_SCISSOR_TEST);
      return;
    }
    const auto v = GetViewport();
    const auto r = *rect;
    glEnable(GL_SCISSOR_TEST);
    glScissor(v[0] + r[0] * v[2] / 960, v[1] + (544 - r[1] - r[3]) * v[3] / 544,
              r[2] * v[2] / 960, r[3] * v[3] / 544);
  }
  std::unique_ptr<SceneRenderState> SaveState() override {
    return std::make_unique<State>();
  }
  void DrawExternal(const std::function<void()>& draw) override {
    auto state = SaveState();
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    draw();
    CheckGl("external scene pass");
  }
  void DrawParticles(std::span<const SceneParticleVertex, 4> vertices,
                     SceneTexture t, const SceneParticleUniforms& u) override {
    if (u.AlphaFamily >= ParticlePrograms.size())
      throw FormatError("invalid particle alpha family");
    auto state = SaveState();
    auto& p = ParticlePrograms[u.AlphaFamily];
    if (!p) {
      std::string fragment = ParticleFragmentShaders[u.AlphaFamily];
      const std::string old = "uniform vec4 tint;";
      fragment.replace(fragment.find(old), old.size(),
                       "in vec4 particleColor;\n#define tint particleColor");
      const std::string uv = "in vec2 texturePosition;";
      fragment.replace(
          fragment.find(uv), uv.size(),
          "in vec2 particleUv;\n#define texturePosition particleUv");
      p = Link(ParticleMode0RasterVertexShader, fragment.c_str());
    }
    glUseProgram(p);
    glUniform4fv(glGetUniformLocation(p, "nativeUniforms"), 13,
                 u.Projection.data());
    glUniform4fv(glGetUniformLocation(p, "nativeViewportFlip"), 1,
                 u.Flip.data());
    glUniform2fv(glGetUniformLocation(p, "nativeTargetSize"), 1,
                 u.TargetSize.data());
    glUniform1f(glGetUniformLocation(p, "viewportEnabled"), u.ViewportEnabled);
    glUniform1f(glGetUniformLocation(p, "nativeDepthOffset"), u.DepthOffset);
    glUniform1f(glGetUniformLocation(p, "nativeDepthScale"), u.DepthScale);
    glUniform1i(glGetUniformLocation(p, "presentToPc"), u.PresentToPc);
    glUniform1f(glGetUniformLocation(p, "nativeAlphaThreshold"),
                u.AlphaThreshold);
    glUniform1i(glGetUniformLocation(p, "image"), 0);
    SetBlend(u.Blend);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, t.Handle);
    glBindVertexArray(ParticleVao);
    glBindBuffer(GL_ARRAY_BUFFER, ParticleBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(SceneParticleVertex) * 4,
                 vertices.data(), GL_STREAM_DRAW);
    constexpr GLint count[]{3, 4, 2, 3, 3};
    constexpr size_t offset[]{0, 12, 16, 24, 36};
    for (GLuint i = 0; i < 5; ++i) {
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, count[i], i == 1 ? GL_UNSIGNED_BYTE : GL_FLOAT,
                            i == 1 ? GL_TRUE : GL_FALSE,
                            sizeof(SceneParticleVertex),
                            reinterpret_cast<void*>(offset[i]));
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CheckGl("scene particles");
  }
  std::unique_ptr<SceneRenderTarget> CreateTarget(int w, int h) override {
    auto state = SaveState();
    auto t = std::make_unique<Target>();
    t->Texture = {Impacto::Renderer->CreateRenderTexture(w, h), uint32_t(w),
                  uint32_t(h), true};
    Impacto::Renderer->ConfigureTextureSampling(t->Texture.Handle,
                                                TextureSampling::LinearClamp);
    glGenFramebuffers(1, &t->Fbo);
    GLC::BindFramebuffer(GL_DRAW_FRAMEBUFFER, t->Fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, t->Texture.Handle, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
      throw FormatError("scene target incomplete");
    return t;
  }
  void SetTarget(const SceneRenderTarget& target) override {
    const auto& t = static_cast<const Target&>(target);
    GLC::BindFramebuffer(GL_DRAW_FRAMEBUFFER, t.Fbo);
    SetViewport({0, 0, int(t.Texture.Width), int(t.Texture.Height)});
  }
  void CopyTarget(SceneRenderTarget& target, std::array<int, 4> v) override {
    auto state = SaveState();
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DITHER);
    GLint source = 0, buffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &source);
    glGetIntegerv(GL_DRAW_BUFFER0, &buffer);
    auto& t = static_cast<Target&>(target);
    GLC::BindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glReadBuffer(buffer);
    GLC::BindFramebuffer(GL_DRAW_FRAMEBUFFER, t.Fbo);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(v[0], v[1], v[0] + v[2], v[1] + v[3], 0, 0,
                      t.Texture.Width, t.Texture.Height, GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);
    CheckGl("copy scene target");
  }
  void PresentTarget(const SceneRenderTarget& target,
                     std::array<int, 4> v) override {
    auto state = SaveState();
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DITHER);
    const auto& t = static_cast<const Target&>(target);
    GLC::BindFramebuffer(GL_READ_FRAMEBUFFER, t.Fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, t.Texture.Width, t.Texture.Height, v[0], v[1],
                      v[0] + v[2], v[1] + v[3], GL_COLOR_BUFFER_BIT, GL_LINEAR);
    CheckGl("present scene target");
  }
  std::vector<uint8_t> ReadTarget(const SceneRenderTarget* target,
                                  std::array<int, 4> v) override {
    auto state = SaveState();
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DITHER);
    GLint source = 0, buffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &source);
    glGetIntegerv(GL_DRAW_BUFFER0, &buffer);
    if (target) {
      source = static_cast<const Target&>(*target).Fbo;
      buffer = GL_COLOR_ATTACHMENT0;
    }
    GLC::BindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glReadBuffer(buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
    std::vector<uint8_t> pixels(size_t(v[2]) * v[3] * 4);
    glReadPixels(v[0],
                 (target ? int(target->Texture.Height) : Window->WindowHeight) -
                     v[1] - v[3],
                 v[2], v[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    CheckGl("read scene target");
    const size_t row = size_t(v[2]) * 4;
    for (int y = 0; y < v[3] / 2; ++y)
      std::swap_ranges(pixels.begin() + size_t(y) * row,
                       pixels.begin() + size_t(y + 1) * row,
                       pixels.begin() + size_t(v[3] - 1 - y) * row);
    return pixels;
  }
};
}  // namespace
std::unique_ptr<Scene2D> CreateScene2DPass() {
  return std::make_unique<Pass>();
}
}  // namespace Impacto::OpenGL
