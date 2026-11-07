// Existing OpenGL-only developer comparisons. Normal game rendering uses Scene2D.
struct ReferenceGlUiBlitRenderer {
  GLuint Program = 0, Vertex = 0, Fragment = 0, Vao = 0, Buffer = 0;
  GLint Tint = -1, Solid = -1, AlphaMode = -1;
  GLint FilterMode = -1, FilterValue = -1, Glyph = -1, AlphaGlyph = -1;
  GLint ColoredRule = -1, ImageRule = -1, RuleValue = -1;
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
  explicit ReferenceGlUiBlitRenderer(const char* referenceFragment = nullptr) {
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
      glUniform1i(glGetUniformLocation(Program, "ruleMask"), 1);
      Tint = glGetUniformLocation(Program, "tint");
      Solid = glGetUniformLocation(Program, "solid");
      AlphaMode = glGetUniformLocation(Program, "alphaMode");
      FilterMode = glGetUniformLocation(Program, "filterMode");
      FilterValue = glGetUniformLocation(Program, "filterValue");
      Glyph = glGetUniformLocation(Program, "glyph");
      AlphaGlyph = glGetUniformLocation(Program, "alphaGlyph");
      ColoredRule = glGetUniformLocation(Program, "coloredRule");
      ImageRule = glGetUniformLocation(Program, "imageRule");
      RuleValue = glGetUniformLocation(Program, "ruleValue");
      GlCheck("UI renderer initialization");
    } catch (...) {
      Release();
      throw;
    }
  }
  void SetAlpha(int mode){glUniform1i(AlphaMode,mode);}
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
  void UploadSlotThumbnail(uint32_t slot,const SlotThumbnail& thumbnail) {
    if(slot>=NativeSlotData::SlotCount || thumbnail.Format!=4 || thumbnail.Pixels.size()!=192*108*2)
      throw FormatError("invalid slot thumbnail surface");
    const ProfileTextureKey key{ProfileTextureSection::SlotThumbnail,-1,slot};
    if(Textures.contains(key))throw FormatError("slot thumbnail is owned by another screen");
    GLuint handle=0;glGenTextures(1,&handle);
    try {
      glBindTexture(GL_TEXTURE_2D,handle);
      // Native b5b9e assembly: linear192x108, one mip, LINEAR min/mag.
      // InitLinear's default U/V mode is CLAMP. Native source rectangles can
      // extend one pixel past width; retain them and clamp the actual sampler.
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      // GXM05001000 U5U6U5_RGB: GL565 plus the GXM RGB swizzle.
      // Keep packed precision, including during bilinear filtering.
      const GLint swizzle[]={GL_BLUE,GL_GREEN,GL_RED,GL_ONE};
      glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,swizzle);
      glPixelStorei(GL_UNPACK_ALIGNMENT,2);
      glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
      glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_FALSE);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGB565,192,108,0,GL_RGB,
                   GL_UNSIGNED_SHORT_5_6_5,thumbnail.Pixels.data());
      GlCheck("packed slot thumbnail upload");
      Textures.emplace(key,BoundTexture{handle,192,108});
    }catch(...){glDeleteTextures(1,&handle);throw;}
  }
  void Draw(std::span<const UiSpriteBlit> blits, int width, int height,
            std::span<const UiRenderCommand> commands = {}) {
    if (width <= 0 || height <= 0) return;
    ViewerWindow::BeginFrame();
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
                std::optional<float> coloredRule = {},const BoundTexture* ruleMask=nullptr,bool alphaGlyph=false) {
    if (coloredRule && !std::isfinite(*coloredRule))
      throw FormatError("nonfinite rule shader value");
    glUniform1i(Solid, solid ? 1 : 0);
    glUniform1i(Glyph, glyph ? 1 : 0);
    glUniform1i(AlphaGlyph, alphaGlyph ? 1 : 0);
    glUniform1i(ColoredRule, coloredRule.has_value() && !ruleMask ? 1 : 0);
    glUniform1i(ImageRule, ruleMask ? 1 : 0);
    glUniform1f(RuleValue, coloredRule.value_or(0.0f));
    auto quad = ResolveUiQuad(blit, texture.Width, texture.Height);
    if(alphaGlyph) {
      // Gxm_DrawGlyphQuad starts at texel centers; the far source boundary
      // remains (source+extent)/textureSize, as in800c2756..800c27c6.
      for(auto corner:{0u,2u})quad[corner][2]+=0.5f/float(texture.Width);
      for(auto corner:{0u,1u})quad[corner][3]+=0.5f/float(texture.Height);
    }
    std::array<float, 24> vertices;
    constexpr std::array<size_t, 6> corners{0, 1, 2, 2, 1, 3};
    for (size_t i = 0; i < corners.size(); ++i)
      std::copy(quad[corners[i]].begin(), quad[corners[i]].end(),
                vertices.begin() + i * 4);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    if(ruleMask) {
      glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,ruleMask->Handle);
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D, texture.Handle);
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
  ~ReferenceGlUiBlitRenderer() { Release(); }
};
struct ReferenceParticleGlRenderer {
  const ParticleManager* TextureOwner = nullptr;
  uint64_t TextureGeneration = 0;
  std::map<size_t,ReferenceGlUiBlitRenderer::BoundTexture> Textures;
  GLuint Vao = 0, Buffer = 0;
  std::array<GLuint,7> Programs{};
  ReferenceParticleGlRenderer() {
    glGenVertexArrays(1,&Vao);glGenBuffers(1,&Buffer);
  }
  ~ReferenceParticleGlRenderer() {
    ResetTextures();
    for (auto program : Programs) if (program) glDeleteProgram(program);
    if (Buffer) glDeleteBuffers(1,&Buffer);
    if (Vao) glDeleteVertexArrays(1,&Vao);
  }
  void ResetTextures() {
    for (const auto& [index,texture] : Textures) glDeleteTextures(1,&texture.Handle);
    Textures.clear(); TextureOwner=nullptr;
  }
  const ReferenceGlUiBlitRenderer::BoundTexture& Texture(const ParticleManager& manager,size_t index) {
    if (TextureOwner!=&manager || TextureGeneration!=manager.ResourceGeneration()) {
      ResetTextures();TextureOwner=&manager;TextureGeneration=manager.ResourceGeneration();
    }
    if (const auto found=Textures.find(index);found!=Textures.end()) return found->second;
    ReferenceGlState scope;
    const auto pixels=DecodeSceneImage(manager.TextureData(index)); GLuint handle=0;glGenTextures(1,&handle);
    const auto found=Textures.emplace(index,ReferenceGlUiBlitRenderer::BoundTexture{handle,pixels.Width,pixels.Height}).first;
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
    const auto vertex = ReferenceGlUiBlitRenderer::Compile(GL_VERTEX_SHADER,ParticleMode0RasterVertexShader);
    GLuint fragment = 0, program = 0;
    try {
      fragment=ReferenceGlUiBlitRenderer::Compile(GL_FRAGMENT_SHADER,ParticleSceneFragmentShaders[family]);
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
    ReferenceGlState scope;
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
