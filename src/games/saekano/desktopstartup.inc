// SceneInit presentation (8002e1da..8002eab0). Resource initialization is
// synchronous on PC; presentation still owns its original logo/movie lifetime.
bool RunStartupPresentation(ViewerWindow& window,
    MusicPlayback& playback,MoviePlayback& movie,PcMovieFrameLayer& movieFrame,
    SemanticState& owner,const std::filesystem::path& root,const Bytes& first,
    const std::filesystem::path& captureDirectory={}) {
  struct Logo {int32_t Fade,Hold,Voice;std::vector<int32_t> Skip;};
  const auto bytes=ReadGameFile(root, Profile::Saekano::GameAsset::StartupEnvironment);PayloadReader reader(bytes);
  for(int i=0;i<3;++i)reader.U8();
  std::array<int32_t,3> skipIndex;for(auto& i:skipIndex)i=reader.I32();
  const auto voiceSkipIndex=reader.I32();
  const auto count=reader.U32();reader.CheckCount(count,16);
  std::vector<Logo> logos;
  for(uint32_t i=0;i<count;++i) {
    Logo logo{reader.I32(),reader.I32(),reader.I32(),{}};
    const auto n=reader.U32();reader.CheckCount(n,4);
    for(uint32_t j=0;j<n;++j)logo.Skip.push_back(reader.I32());
    if(logo.Fade<0 || logo.Hold<0 || logo.Voice!=-1)
      throw UnsupportedCommand("startup logo timing/voice is not supported");
    for(const auto index:skipIndex)if(index<0 || size_t(index)>=logo.Skip.size() || logo.Skip[size_t(index)]<0)
      throw FormatError("invalid startup logo skip duration");
    if(voiceSkipIndex<0 || size_t(voiceSkipIndex)>=logo.Skip.size())throw FormatError("invalid startup voice skip index");
    logos.push_back(std::move(logo));
  }
  const auto debug=ReadGameFile(root, Profile::Saekano::GameAsset::DebugEnvironment);
  if(debug.size()<35 || !owner.SystemProgress)throw FormatError("startup requires debug flags and system progress");
  const bool capture=!captureDirectory.empty();
  GpdaArchive outer(first),archive(outer.Data(2));
  if(archive.Entries().size()!=logos.size())throw FormatError("startup logo catalogue mismatch");
  UiBlitRenderer logoRenderer;
  std::ofstream trace;if(capture)trace.open(captureDirectory/"startup.jsonl");
  const uint64_t epoch=SDL_GetTicks();uint64_t frame=0,previous=0;
  const auto now=[&]{return capture?frame*16:SDL_GetTicks()-epoch;};
  bool quit=false;
  const auto input=[&] {
    bool accept=false;SDL_Event event{};
    while(ViewerWindow::PollEvent(&event)) {
      if(event.type==SDL_EVENT_QUIT)quit=true;
      if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key==SDLK_RETURN)accept=true;
    }
    return accept;
  };
  const auto begin=[&] {
    playback.Update(float(now()-previous)/1000);previous=now();
    if(capture){std::array<int16_t,1536> samples{};playback.Render(samples);}
    int width=0,height=0;SdlCheck(SDL_GetWindowSizeInPixels(window.Window,&width,&height),"startup viewport");
    logoRenderer.Draw({},width,height);
  };
  const auto finish=[&] {
    SdlCheck(window.Present(),"startup swap");++frame;
    if(capture && frame>=16000)throw FormatError("startup capture exhausted its bounded frame budget");
    // The visible window already uses swap interval1. A second16ms wait here
    // would halve the frame-counter logo animation's speed.
  };
  if(!debug[33] && !RuntimePcSettings().SkipLogos)for(size_t i=0;i<logos.size() && !quit;++i) {
    const auto& logo=logos[i];const ProfileTextureKey key{ProfileTextureSection::SharedUi,-2,uint32_t(i)};
    const auto& entry=archive.Entries().at(i);
    if(entry.Type!=1)throw FormatError("startup logo requires gzip GXT");
    logoRenderer.Upload(key,DecodeSceneImage(InflateGzip(archive.Data(i))));
    SlotFrameChannel opacity,hold;opacity.Start(0,255,logo.Fade);hold.Start(0,logo.Hold,logo.Hold);
    int phase=0;bool accelerated=false,saved=false;
    while(phase<3 && !quit) {
      const bool accept=input();auto& channel=phase==1?hold:opacity;
      if(accept && !accelerated) {
        const auto remaining=int64_t(channel.Frames-channel.Elapsed)*logo.Skip[size_t(skipIndex[size_t(phase)])]/channel.Frames;
        channel.Start(channel.Current,channel.To,int32_t(remaining));accelerated=true;
      }
      channel.Tick();begin();
      UiSpriteBlit sprite;sprite.Texture=key;sprite.Source={0,0,960,544};sprite.Destination={0,0,960,544};
      sprite.Color={255,255,255,uint8_t(opacity.Current)};
      logoRenderer.DrawQuad(sprite,logoRenderer.Textures.at(key),false);
      if(capture && phase==1 && !saved) {
        Capture(captureDirectory/("logo-"+std::to_string(i)+".bmp"),960,544);saved=true;
        trace<<"{\"logo\":"<<i<<",\"frame\":"<<frame<<"}\n";
      }
      if(channel.Elapsed>=channel.Frames) {
        ++phase;accelerated=false;if(phase==2)opacity.Start(255,0,logo.Fade);
      }
      finish();
    }
  }
  if(!quit && !debug[34]) {
    const int32_t volume=int32_t(owner.SystemProgress->Bytes6022c[7])*10;
    movieFrame.Retire();movie.Prepare(Profile::Saekano::AssetPath(root, Profile::Saekano::GameAsset::OpeningMovie));movie.SetVolume(volume);
    auto& viewed=owner.SystemProgress->Histories[4];
    if(viewed.Values.empty())viewed.Values.resize(1,viewed.Fallback);viewed.Values[0]=1;
    while(!movie.Ready() && !quit){input();movie.Update(now());begin();finish();SDL_Delay(1);}
    if(!quit) {
      const auto started=now();movie.Start(started);
      TitleTween fade;bool skipping=false,saved=false;
      const auto duration=GlobalUiEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment)).FadeDuration;
      while(!movie.State().Finished && !quit) {
        const bool accept=input();movie.Update(now());
        if(!skipping && now()-started>=1000 && (accept || (capture && movie.State().VideoPresented>=6))) {
          fade.Configure(0,100,duration,true,now());skipping=true;
        }
        begin();movieFrame.Draw();
        if(capture && !saved && movie.State().VideoPresented>=6) {
          Capture(captureDirectory/"opening.bmp",960,544);saved=true;
          trace<<"{\"movie_frames\":"<<movie.State().VideoPresented<<",\"audio_queued\":"<<movie.State().AudioQueued<<"}\n";
        }
        if(skipping) {
          fade.Tick(now());movie.SetVolume(volume*(100-fade.Value.Current)/100);
          // Movie drawing restores its previous program; select the UI owner
          // explicitly before the startup fade, just as the native draw does.
          logoRenderer.Bind();
          UiSpriteBlit white;white.Destination={0,0,960,544};white.Color={255,255,255,uint8_t(fade.Value.Current*255/100)};
          logoRenderer.DrawQuad(white,{0,1,1},true);
          if(fade.Finished())movie.Stop();
        }
        finish();if(capture)SDL_Delay(1);
      }
      if(capture && !saved)throw FormatError("startup opening did not present decoded video");
    }
    movie.Stop();movieFrame.Retire();movie.SetVolume(volume);
  }
  return !quit;
}
