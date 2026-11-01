int RunSceneThumbnailProbe(const std::filesystem::path& output) {
  bool created=false;
  try {
    if(std::filesystem::exists(output))throw FormatError("choose a fresh scene thumbnail output");
    std::filesystem::create_directories(output);created=true;
    ViewerWindow window(true);UiBlitRenderer renderer;
    const auto require=[](bool v,const char* s){if(!v)throw FormatError(s);};
    const auto color=[](int x,int y)->std::array<uint8_t,4>{
      return {uint8_t(x*17+(y/3)*7),uint8_t(y*13+x/5),uint8_t(x^(y*11)),uint8_t(x+y)};
    };
    constexpr int width=966,height=554,ox=3,oy=5;
    Bytes pixels(width*height*4,255);
    for(int y=0;y<544;++y)for(int x=0;x<960;++x){const auto c=color(x,543-y);
      std::copy(c.begin(),c.end(),pixels.begin()+((y+oy)*width+x+ox)*4);}
    struct Source {
      GLuint Texture=0,Fbo=0;GLuint Pbos[2]{};
      ~Source(){glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glDeleteBuffers(2,Pbos);if(Fbo)glDeleteFramebuffers(1,&Fbo);if(Texture)glDeleteTextures(1,&Texture);}
    } source;
    glGenTextures(1,&source.Texture);glBindTexture(GL_TEXTURE_2D,source.Texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glGenFramebuffers(1,&source.Fbo);glBindFramebuffer(GL_FRAMEBUFFER,source.Fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source.Texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"fixture framebuffer incomplete");
    glGenBuffers(2,source.Pbos);
    for(size_t i=0;i<2;++i){const auto target=i?GL_PIXEL_UNPACK_BUFFER:GL_PIXEL_PACK_BUFFER;
      glBindBuffer(target,source.Pbos[i]);glBufferData(target,32,nullptr,GL_STATIC_DRAW);}
    glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,31);
    glPixelStorei(GL_PACK_SKIP_ROWS,3);glPixelStorei(GL_PACK_SKIP_PIXELS,2);glPixelStorei(GL_PACK_SWAP_BYTES,GL_TRUE);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,source.Texture);
    glViewport(ox,oy,960,544);glEnable(GL_SCISSOR_TEST);glScissor(2,3,7,11);
    glEnable(GL_FRAMEBUFFER_SRGB);glEnable(GL_DITHER);
    const auto state=[](){
      std::vector<GLint> r;
      for(const auto key:{GL_READ_FRAMEBUFFER_BINDING,GL_DRAW_FRAMEBUFFER_BINDING,GL_READ_BUFFER,GL_DRAW_BUFFER0,
          GL_ACTIVE_TEXTURE,GL_TEXTURE_BINDING_2D,GL_PIXEL_PACK_BUFFER_BINDING,GL_PIXEL_UNPACK_BUFFER_BINDING,
          GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PACK_SWAP_BYTES}){
        GLint v=0;glGetIntegerv(key,&v);r.push_back(v);}
      for(const auto key:{GL_SCISSOR_TEST,GL_FRAMEBUFFER_SRGB,GL_DITHER})r.push_back(glIsEnabled(key));
      for(const auto key:{GL_VIEWPORT,GL_SCISSOR_BOX}){std::array<GLint,4> v{};glGetIntegerv(key,v.data());r.insert(r.end(),v.begin(),v.end());}
      return r;
    };
    NativeSlotRecord record;record.Thumbnail.emplace();record.Thumbnail->Pixels.resize(192*108*2,0x5a);
    record.ThumbnailPresent=1;record.Word1ba8=0x12345678;record.Strings[2]={77};
    const auto before=record.Encode();const auto expectedState=state();
    std::vector<GLuint> textures,fbos;Bytes rgba,packed;int maxError=0;
    {
      PcSceneThumbnail capture;
      bool rejected=false;try{capture.PublishTo(record);}catch(const UnsupportedCommand&){rejected=true;}
      require(rejected && record.Encode()==before,"unready capture replaced imported thumbnail");
      capture.Publish({ox,oy,960,544});require(capture.Ready() && state()==expectedState,"scene publication changed GL state");
      rgba=capture.CaptureRgba();require(state()==expectedState,"reduction/readback changed GL state");
      for(int y=0;y<108;++y)for(int x=0;x<192;++x) {
        const double px=(x+.5)*960/192-.5,py=(y+.5)*544/108-.5;
        const int ix=int(std::floor(px)),iy=int(std::floor(py));const double fx=px-ix,fy=py-iy;
        const auto a=color(ix,iy),b=color(ix+1,iy),c=color(ix,iy+1),d=color(ix+1,iy+1);
        for(size_t channel=0;channel<4;++channel){const int expected=int(std::floor(
            (a[channel]*(1-fx)+b[channel]*fx)*(1-fy)+(c[channel]*(1-fx)+d[channel]*fx)*fy+.5));
          maxError=std::max(maxError,std::abs(int(rgba[(y*192+x)*4+channel])-expected));}
      }
      require(maxError<=1,"GPU reduction differs from centered linear sampling");
      packed=capture.Capture().Pixels;
      require(packed==PackSceneThumbnailRgba(rgba).Pixels,"capture packing differs from reduced image");
      glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_FRONT);
      const auto splitBindings=state();
      capture.Publish({ox,oy,960,544});require(capture.CaptureRgba()==rgba,"split framebuffer capture used wrong source");
      require(state()==splitBindings,"split read/draw framebuffer state changed");
      glBindFramebuffer(GL_READ_FRAMEBUFFER,source.Fbo);
      require(state()==expectedState,"source framebuffer read selector changed");
      // Simulate later menu pixels overwriting the original target.
      glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);
      require(capture.CaptureRgba()==rgba,"capture borrowed mutable scene storage");
      capture.PublishTo(record);const auto saved=record.Thumbnail;
      require(record.ThumbnailPresent==1 && saved->Pixels==packed,"working thumbnail publication differs");
      record.Thumbnail=NativeSlotRecord::Decode(before).Thumbnail;
      require(record.Encode()==before,"thumbnail publication changed other record fields");
      rejected=false;try{capture.Publish({0,0,0,34});}catch(const FormatError&){rejected=true;}
      require(rejected && capture.CaptureRgba()==rgba,"failed publication lost completed scene");
      capture.Publish({0,0,60,34});const auto replaced=capture.CaptureRgba();
      for(size_t i=0;i<replaced.size();i+=4)require(replaced[i]==255 && replaced[i+1]==0 && replaced[i+2]==0,"resized scene did not replace capture");
      require(state()==expectedState,"replacement changed GL state");
      for(const auto& f:capture.Frames){textures.push_back(f.Texture);fbos.push_back(f.Fbo);}
      textures.push_back(capture.Reduced.Texture);fbos.push_back(capture.Reduced.Fbo);
    }
    for(auto t:textures)require(t && !glIsTexture(t),"scene capture texture did not retire");
    for(auto f:fbos)require(f && !glIsFramebuffer(f),"scene capture framebuffer did not retire");
    require(glIsTexture(source.Texture) && glIsFramebuffer(source.Fbo),"capture retired borrowed resources");
    // The actual scenario host draws to the window back buffer, whose read
    // buffer selector need not match its draw selector.
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glReadBuffer(GL_FRONT);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_DITHER);
    glClearColor(41.0f/255,137.0f/255,223.0f/255,1);glClear(GL_COLOR_BUFFER_BIT);
    glViewport(7,9,960,544);const auto windowState=state();
    {
      PcSceneThumbnail capture;capture.Publish({7,9,960,544});const auto image=capture.CaptureRgba();
      for(size_t i=0;i<image.size();i+=4)require(std::abs(int(image[i])-41)<=1 &&
        std::abs(int(image[i+1])-137)<=1 && std::abs(int(image[i+2])-223)<=1,"window capture used wrong buffer");
      require(state()==windowState,"window capture changed GL state");
    }
    GlCheck("scene thumbnail focused checks");
    const auto write=[&](const char* name,const Bytes& data){std::ofstream f(output/name,std::ios::binary);
      f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));if(!f)throw FormatError("cannot save capture witness");};
    write("reduced-rgba.bin",rgba);write("packed-format4.bin",packed);
    std::ofstream report(output/"capture.json");report<<"{\"passed\":true,\"pixels\":20736,\"max_linear_error_u8\":"<<maxError
      <<",\"unready_rejected\":true,\"owned_copy\":true,\"viewport_crop\":true,\"orientation\":true,\"resized_replacement\":true,"
        "\"gl_state_restored\":true,\"split_read_draw_bindings\":true,\"window_back_buffer\":true,\"resources_retired\":true,\"imported_record_preserved\":true,\"native_pixel_parity\":false}\n";
    if(!report)throw FormatError("cannot save capture report");return 0;
  }catch(const std::exception& e){
    if(created && !std::filesystem::exists(output/"error.txt")){std::ofstream f(output/"error.txt");f<<e.what()<<'\n';}
    return 1;
  }
}
