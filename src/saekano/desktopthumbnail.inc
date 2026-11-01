// Included inside desktop.cpp's private host namespace. The owning GL context
// outlives this adapter. Follow the shared renderer's framebuffer-copy pattern.
class PcSceneThumbnail {
  struct Bindings {
    GLint Read=0,Draw=0,ReadBuffer=0,DrawReadBuffer=0,Texture=0,Pack=0,Unpack=0;
    GLint Alignment=0,RowLength=0,SkipRows=0,SkipPixels=0,SwapBytes=0;
    GLboolean Scissor=0,Srgb=0,Dither=0;
    Bindings() {
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&Read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&Draw);
      glGetIntegerv(GL_READ_BUFFER,&ReadBuffer);glGetIntegerv(GL_TEXTURE_BINDING_2D,&Texture);
      glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(Draw));glGetIntegerv(GL_READ_BUFFER,&DrawReadBuffer);
      glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(Read));
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&Pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&Unpack);
      glGetIntegerv(GL_PACK_ALIGNMENT,&Alignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&RowLength);
      glGetIntegerv(GL_PACK_SKIP_ROWS,&SkipRows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&SkipPixels);
      glGetIntegerv(GL_PACK_SWAP_BYTES,&SwapBytes);
      Scissor=glIsEnabled(GL_SCISSOR_TEST);Srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);Dither=glIsEnabled(GL_DITHER);
      glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
      glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
    }
    ~Bindings() {
      glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(Draw));glReadBuffer(GLenum(DrawReadBuffer));
      glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(Read));glReadBuffer(GLenum(ReadBuffer));
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(Draw));glBindTexture(GL_TEXTURE_2D,GLuint(Texture));
      glBindBuffer(GL_PIXEL_PACK_BUFFER,GLuint(Pack));glBindBuffer(GL_PIXEL_UNPACK_BUFFER,GLuint(Unpack));
      glPixelStorei(GL_PACK_ALIGNMENT,Alignment);glPixelStorei(GL_PACK_ROW_LENGTH,RowLength);
      glPixelStorei(GL_PACK_SKIP_ROWS,SkipRows);glPixelStorei(GL_PACK_SKIP_PIXELS,SkipPixels);
      glPixelStorei(GL_PACK_SWAP_BYTES,SwapBytes);
      Restore(GL_SCISSOR_TEST,Scissor);Restore(GL_FRAMEBUFFER_SRGB,Srgb);Restore(GL_DITHER,Dither);
    }
    static void Restore(GLenum cap,GLboolean on){if(on)glEnable(cap);else glDisable(cap);}
  };
  struct Surface {
    GLuint Texture=0,Fbo=0;int Width=0,Height=0;
    Surface()=default;Surface(const Surface&)=delete;Surface& operator=(const Surface&)=delete;
    ~Surface(){if(Fbo)glDeleteFramebuffers(1,&Fbo);if(Texture)glDeleteTextures(1,&Texture);}
    void Allocate(int width,int height) {
      if(!Texture)glGenTextures(1,&Texture);
      glBindTexture(GL_TEXTURE_2D,Texture);
      if(Width!=width || Height!=height) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        GlCheck("scene capture allocation");Width=width;Height=height;
      }
      if(!Fbo)glGenFramebuffers(1,&Fbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER,Fbo);
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,Texture,0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw FormatError("scene capture framebuffer incomplete");
    }
  };
  std::array<Surface,2> Frames;
  Surface Reduced;
  size_t Current=0;
  bool Published=false;
  friend int ::Impacto::Saekano::RunSceneThumbnailProbe(const std::filesystem::path&);
 public:
  PcSceneThumbnail()=default;
  PcSceneThumbnail(const PcSceneThumbnail&)=delete;PcSceneThumbnail& operator=(const PcSceneThumbnail&)=delete;
  bool Ready() const{return Published;}
  void Present(std::array<GLint,4> viewport) const {
    if(!Ready())throw FormatError("scene modal has no completed background");
    Bindings old;const auto& frame=Frames[Current];
    glBindFramebuffer(GL_READ_FRAMEBUFFER,frame.Fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,0,frame.Width,frame.Height,viewport[0],viewport[1],
      viewport[0]+viewport[2],viewport[1]+viewport[3],GL_COLOR_BUFFER_BIT,GL_LINEAR);
    GlCheck("scene modal background");
  }
  // Called only after a complete scene draw, before separate menu/modal draws.
  // Viewport is in framebuffer pixels, including its actual origin.
  void Publish(std::array<GLint,4> viewport) {
    if(viewport[0]<0 || viewport[1]<0 || viewport[2]<=0 || viewport[3]<=0)
      throw FormatError("invalid completed scene capture viewport");
    Bindings old;GLint sourceBuffer=0;glGetIntegerv(GL_DRAW_BUFFER0,&sourceBuffer);
    auto& next=Frames[1-Current];next.Allocate(viewport[2],viewport[3]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(old.Draw));glReadBuffer(GLenum(sourceBuffer));
    glBindTexture(GL_TEXTURE_2D,next.Texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,viewport[0],viewport[1],viewport[2],viewport[3]);
    GlCheck("completed scene capture copy");Current=1-Current;Published=true;
  }
  Bytes CaptureRgba() {
    if(!Ready())throw UnsupportedCommand("scene thumbnail requires a completed scene frame");
    Bindings old;const auto& frame=Frames[Current];Reduced.Allocate(192,108);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,frame.Fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0,0,frame.Width,frame.Height,0,0,192,108,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,Reduced.Fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE);
    Bytes rgba(192*108*4);glReadPixels(0,0,192,108,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    GlCheck("reduced scene capture readback");
    for(size_t y=0;y<54;++y)std::swap_ranges(rgba.begin()+y*192*4,rgba.begin()+(y+1)*192*4,rgba.begin()+(107-y)*192*4);
    return rgba;
  }
  SlotThumbnail Capture(){return PackSceneThumbnailRgba(CaptureRgba());}
  void PublishTo(NativeSlotRecord& record) {
    auto value=Capture();record.Thumbnail=std::move(value);record.ThumbnailPresent=1;
  }
};
