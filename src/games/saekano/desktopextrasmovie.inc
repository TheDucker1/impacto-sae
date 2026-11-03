// Real movie child. The parent owns shared font/UI resources and supplies the
// title audio switch; no synthetic playback state or replacement thumbnail.
class PcExtrasMovie final:public ExtrasMovieServices {
 public:
  PcExtrasMovie(SceneDialogLayer& shared,SemanticState& owner,PlaybackTitleAudio& audio,
      MusicPlayback& playback,const std::filesystem::path& root,
      std::span<const int32_t> repeats,uint32_t hold,bool unlockAll,
      std::function<void()> restoreMusic,uint64_t now)
      :shared_(shared),owner_(owner),audio_(audio),root_(std::filesystem::canonical(root)),
       restoreMusic_(std::move(restoreMusic)),env_(ExtrasScreenEnvironment::DecodeThroughMovies(ReadGameFile(root, Profile::Saekano::GameAsset::ExtrasEnvironment))),
       help_(ExtrasButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment))),
       childHelp_(ExtrasChildButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment))),
       style_(ButtonHelpStyle::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
       markerEnv_(NewMarkerEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
       marker_(StartNewMarker(markerEnv_,now)),screen_(env_,owner,*this,repeats,hold,unlockAll,now),now_(now){
    if(!restoreMusic_ || childHelp_.MovieRows.empty())throw FormatError("movie child services/help are missing");
    if(shared.Resources.ExtrasMovieThumbnailCount()!=env_.Movies.size())throw FormatError("movie thumbnail/catalogue counts differ");
    for(const auto& [key,texture]:shared.Renderer.Textures){(void)texture;prior_.insert(key);}
    try {
      for(size_t i=0;i<env_.Movies.size();++i){
        const ProfileTextureKey key{ProfileTextureSection::ExtrasMovieThumbnail,-1,uint32_t(i)};
        if(shared.Renderer.Textures.contains(key))throw FormatError("movie thumbnail is owned by another screen");
        shared.Renderer.Upload(key,DecodeSceneImage(shared.Resources.LoadProfileTexture(key).Data));
      }
      playback.Submit({GameEventKind::StopBgm,0});
      movie_.Present=[this](const AVFrame* frame,int64_t pts){frame_.Present(frame,pts);};
    }catch(...){RetireTextures();throw;}
  }
  ~PcExtrasMovie(){movie_.Stop();frame_.Retire();RetireTextures();}
  void BeginFrame(const ExtrasMovieInput& input,const WidgetInputHistory::Snapshot& contacts,uint64_t now){
    input_=input;now_=now;screen_.BeginFrame(contacts);
  }
  void CancelPointer(){screen_.CancelPointer();}
  int32_t Update(uint64_t now){
    now_=now;movie_.Update(now);const auto result=screen_.Update(input_,now);
    if(!result)UpdateNewMarker(marker_,markerEnv_,now);return result;
  }
  bool Dirty()const{return screen_.Dirty();}
  const ExtrasMovieBrowser& State()const{return screen_;}
  const MoviePlaybackState& PlaybackState()const{return movie_.State();}
  bool ResourcesReady()const override{
    for(size_t i=0;i<env_.Movies.size();++i)
      if(!shared_.Renderer.Textures.contains({ProfileTextureSection::ExtrasMovieThumbnail,-1,uint32_t(i)}))return false;
    return true;
  }
  void PrepareMovie(size_t index)override{
    const auto& name=env_.Movies.at(index).Name;
    const auto path=std::filesystem::weakly_canonical(root_/name);
    if(name.empty() || path.parent_path()!=root_)throw FormatError("Extras movie resolves outside USRDIR");
    frame_.Retire();movie_.Prepare(path);
    movie_.SetVolume(int32_t(owner_.SystemProgress->Bytes6022c[7])*10);
  }
  void ReprepareMovie()override{frame_.Retire();movie_.Restart();}
  bool MovieReady()const override{return movie_.Ready();}
  bool MovieBusy()const override{return movie_.State().Prepared && !movie_.State().Finished;}
  bool MovieIdle()const override{return !movie_.State().Prepared || movie_.State().Finished;}
  bool MoviePaused()const override{return movie_.State().Paused;}
  void PauseMovie()override{movie_.SetPaused(true,now_);}
  void StartMovie()override{movie_.Start(now_);}
  void StopMovie()override{movie_.Stop();}
  uint64_t MoviePositionUs()const override{return movie_.State().Prepared?uint64_t(std::max(int64_t(0),movie_.State().PositionUs)):0;}
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  void RestoreExtrasMusic()override{restoreMusic_();}
  void Draw(){
    ScopedSceneRenderState scope;BindUi();
    const auto rectangle=[&](std::array<int32_t,4> r,std::array<uint8_t,4> color){UiSpriteBlit b;b.Destination=r;b.Color=color;shared_.Renderer.DrawQuad(b,{0,1,1},true);};
    const auto sprite=[&](uint32_t id,const UiSpriteDefinition& s){shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SystemUi,-1,id},s));};
    const auto text=[&](const UiTextBlit& t){shared_.Textures.DrawString(shared_.Renderer,shared_.Font,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);};
    const auto centered=[&](std::u16string_view value,int32_t x,int32_t y,uint32_t size){
      const auto width=MeasurePvfText(shared_.Font.Cached(),value,size,shared_.Font.NominalSize());
      text({std::u16string(value),{std::bit_cast<int32_t>(uint32_t(x)-width/2),y},size,{0,0,0,255}});
    };
    if(screen_.Phase()==2 || !ResourcesReady()){
      rectangle({0,0,960,544},{255,255,255,uint8_t(screen_.Fade())});return;
    }
    rectangle({0,0,960,544},{255,255,255,128});
    auto background=env_.Sprites.at(0xd8d4);background.Color[3]=uint8_t(env_.Scalars.at(0xd8f8));sprite(19,background);
    background=env_.Sprites.at(0xd8fc);background.Color[3]=uint8_t(env_.Scalars.at(0xd8f8));sprite(20,background);
    auto fill=ResolveUiSprite({ProfileTextureSection::SystemUi,-1,60},env_.Common.SharedSprites[1]);
    fill.Destination[2]=env_.Common.StretchSize[0];fill.Destination[3]=env_.Common.StretchSize[1];shared_.Sprite(fill);
    sprite(60,env_.Common.SharedSprites[0]);
    // The established list viewport lowers the native stencil/alpha/depth
    // service sequence; the callback's offsets are horizontal in this owner.
    const auto& list=screen_.List();SlotViewportInputs viewport;
    viewport.Visible=true;viewport.Animated=true;viewport.Scrolling=list.Scrolling;viewport.Rows=list.Rows;
    viewport.Rectangle=env_.Rectangles.at(0xd968);viewport.Pitch=list.Settings.Pitch;
    viewport.ScrollOffset=list.Scroll.Current;viewport.CursorOffset=list.Cursor.Current;
    for(const auto& command:BuildSlotViewport(viewport)){
      if(command.Kind!=SlotViewportKind::Cards){
        shared_.Renderer.Execute(std::span<const UiRenderCommand>(&command.Render,1));continue;
      }
      const auto& cards=command.Cards;
      if(screen_.Browsing())for(int32_t i=cards.Begin;i<cards.End;++i){
        if(i<0 || size_t(i)>=env_.Movies.size())continue;
        const int32_t offset=std::bit_cast<int32_t>(uint32_t(env_.Scalars.at(0xd9a4))+
            uint32_t(i-cards.FirstVisible)*uint32_t(list.Settings.Pitch)+uint32_t(cards.Offsets[0]));
        if(screen_.Unlocked(size_t(i))){
          if(screen_.PreviewIndex()==i && !screen_.PreviewHidden()){
            auto r=screen_.MovieRectangle();r[0]=env_.Sprites.at(0xd9e8).X+offset;r[1]=env_.Sprites.at(0xd9e8).Y;
            ScopedSceneRenderState movieState;frame_.Draw(r,{255,255,255,255},true);
          }else {
            auto s=env_.Sprites.at(0xda0c);s.X+=offset;
            shared_.Sprite(ResolveUiSprite({ProfileTextureSection::ExtrasMovieThumbnail,-1,uint32_t(i)},s));
          }
          if(screen_.Status(size_t(i))==1){
            const auto& anchor=env_.Sprites.at(0xda30);
            for(const auto& b:BuildNewMarker(marker_,markerEnv_,{ProfileTextureSection::SharedUi,-1,18},{anchor.X+offset,anchor.Y}))shared_.Sprite(b);
          }
        }else {auto s=env_.Sprites.at(0xd9e8);s.X+=offset;sprite(21,s);}
      }
    }
    const auto& arrows=screen_.Arrows();
    for(size_t i=0;i<2;++i){
      auto s=env_.Sprites.at(i?0xd944:0xd920);s.X+=i?int32_t(arrows.Current):-int32_t(arrows.Current);
      if((i==0 && screen_.Selected()!=list.Rows.Minimum) || (i==1 && screen_.Selected()!=list.Rows.Maximum)){
        auto b=ResolveUiSprite({ProfileTextureSection::SystemUi,-1,56},s);
        if(i)b.Source[2]=-b.Source[2];shared_.Sprite(b);
      }
    }
    sprite(14,env_.Sprites.at(0xda54));
    auto selected=env_.Sprites.at(0xd9c0);selected.X+=env_.Sprites.at(0xda54).X;selected.Y+=env_.Sprites.at(0xda54).Y;
    selected.SourceX+=screen_.Selected()*std::bit_cast<int32_t>(env_.Scalars.at(0xd9e4));sprite(15,selected);
    const auto numberSize=FontSize(0xd9bc);
    for(size_t i=0;i<env_.Movies.size();++i){
      const auto& p=env_.Points.at(0xd9a8);const auto& step=env_.Points.at(0xd9b0);
      const int32_t x=p[0]+int32_t(i)*step[0],y=p[1]-int32_t(numberSize/2);
      centered(std::u16string(1,char16_t(0xff10+(i+1)/10)),x,y,numberSize);
      centered(std::u16string(1,char16_t(0xff10+(i+1)%10)),x+std::bit_cast<int32_t>(env_.Scalars.at(0xd9b8)),y,numberSize);
    }
    const auto& titlePoint=env_.Points.at(0xda78);
    centered(screen_.Unlocked(size_t(screen_.Selected()))?env_.Movies[size_t(screen_.Selected())].Title:env_.Strings.at(0xd89c),titlePoint[0],titlePoint[1],FontSize(0xda80));
    for(size_t i=0;i<2;++i){auto b=ResolveUiSprite({ProfileTextureSection::SharedUi,-1,1},env_.Sprites.at(i?0xdaa8:0xda84));if(i)b.Source[2]=-b.Source[2];shared_.Sprite(b);}
    auto shared=env_.Common.SharedSprites[3];shared.Alignment=0;shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,6},shared));
    auto back=env_.Common.SharedSprites[5];back.Color={255,255,255,255};shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,2},back));
    for(const auto& t:BuildButtonHelp(childHelp_.MovieRows[0],help_.Rectangle,style_,shared_.Font.Cached(),uint32_t(shared_.FontSize),shared_.Font.NominalSize()))text(t);
    if(!screen_.Browsing()){
      const auto r=screen_.MovieRectangle();rectangle(r,{255,255,255,255});frame_.Draw(r);BindUi();
    }
    rectangle({0,0,960,544},{255,255,255,uint8_t(screen_.Fade())});GlCheck("Extras movie presentation");
  }
 private:
  uint32_t FontSize(uint16_t key)const{
    const float v=float(shared_.FontSize)*std::bit_cast<float>(env_.Scalars.at(key));
    if(!std::isfinite(v) || v<0 || v>1024)throw FormatError("invalid Extras movie font size");return uint32_t(v);
  }
  void BindUi(){
    auto& r=shared_.Renderer;r.Bind();

    r.SetAlpha(0);r.SetFilter(0,0);ApplyParticleBlend(1);
  }
  void RetireTextures()noexcept{
    auto& textures=shared_.Renderer.Textures;
    for(auto it=textures.begin();it!=textures.end();){if(!prior_.contains(it->first)){Impacto::Renderer->FreeTexture(it->second.Handle);it=textures.erase(it);}else ++it;}
  }
  SceneDialogLayer& shared_;SemanticState& owner_;PlaybackTitleAudio& audio_;std::filesystem::path root_;
  std::function<void()> restoreMusic_;ExtrasScreenEnvironment env_;
  ExtrasButtonHelpEnvironment help_;ExtrasChildButtonHelpEnvironment childHelp_;ButtonHelpStyle style_;
  NewMarkerEnvironment markerEnv_;NewMarkerState marker_;MoviePlayback movie_;PcMovieFrameLayer frame_;
  ExtrasMovieBrowser screen_;ExtrasMovieInput input_;uint64_t now_;std::set<ProfileTextureKey> prior_;
};
