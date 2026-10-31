// Included in desktop.cpp after the shared UI/font consumers. This owner binds
// the native title controller, rather than manufacturing a later story cursor.
class PcTitleOwner final:public TitleServices {
 public:
  TitleState State;
  TitleInput Input;
  TitleNavigationInput NavigationInput;
  TitleVisualState Visual;
  TitleEffectState Effects;
  TitleWidgetInput Contacts;
  uint64_t Now=0;
  std::optional<int32_t> RequestedScene;
  size_t Resets=0,HistoryClears=0;
  PcTitleOwner(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,
      MusicPlayback& playback,ArchiveBgmResources& music,TitleAudioOutput& output,
      TitleEnvironment environment,TitleSessionSeeds seeds,std::span<const int32_t> repeats,
      uint32_t holdFrames,std::function<void()> clearHistory,uint64_t now)
      :Contacts(environment,holdFrames),Now(now),owner_(owner),resources_(resources),renderer_(renderer),
       playback_(playback),music_(music),audio_(resources,output),env_(std::move(environment)),
       seeds_(std::move(seeds)),navigation_(repeats),clearHistory_(std::move(clearHistory)) {
    if(!owner_.SystemProgress || !owner_.BgmMasterVolume)throw FormatError("title requires complete system owner");
    if(owner_.SceneImages.Known)throw UnsupportedCommand("fresh PC title cannot retire a carried scene surface");
    random_.Seed(uint32_t(now));StartTitleVisuals(Visual,env_,now);StartTitleEffects(Effects,env_,now);
    StartTitle(State,env_,*this,now);
  }
  ~PcTitleOwner(){try{Retire();}catch(...){}}
  const TitleEnvironment& Environment()const{return env_;}
  uint8_t VoiceMode()const override{return owner_.SystemProgress->Byte60239;}
  void ResetSession()override{ResetTitleSession(owner_,seeds_);++Resets;}
  void ClearMessageHistory()override{clearHistory_();++HistoryClears;}
  void BeginResources(uint8_t mode)override{
    if(ready_)throw FormatError("title resource owner is already committed");
    const auto load=[&](ProfileTextureSection section,size_t count){
      for(size_t i=0;i<count;++i){
        const ProfileTextureKey key{section,-1,uint32_t(i)};
        if(renderer_.Textures.contains(key))throw FormatError("title texture is owned by another scene");
        // Track before upload so partial failures also retire their GL handles.
        keys_.push_back(key);renderer_.Upload(key,DecodeSceneImage(resources_.LoadProfileTexture(key).Data));
      }
    };
    load(ProfileTextureSection::TitleUi,resources_.TitleTextureCount());
    load(ProfileTextureSection::SystemUi,resources_.SystemTextureCount());
    audio_.Load(mode);ready_=true;
  }
  bool ResourcesReady()override{return ready_ && audio_.Ready();}
  void CommitResources()override{if(!ResourcesReady())throw FormatError("title resources are incomplete");committed_=true;}
  void PlayBgmAndMark(int32_t id)override{
    if(!committed_)throw FormatError("title BGM has no committed resource owner");
    music_.Prepare(id);
    if(firstMusic_){
      GameEvent event{GameEventKind::PlayBgm,id};event.Bgm=BgmRequest(id,*owner_.BgmMasterVolume,100,music_.Coefficient());
      playback_.Submit(event);firstMusic_=false;
    }else playback_.Submit({GameEventKind::PlayBgmRetainingGain,id});
    MarkMusicPlayed(owner_.MusicHistory,size_t(id));
  }
  void StopBgm()override{playback_.Submit({GameEventKind::StopBgmWithCurrentFade,0});}
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  int32_t RandomRange(int32_t lo,int32_t hi)override{return random_.Range(lo,hi);}
  void PlayEntryVoice(int32_t index)override{audio_.PlayEntry(index);}
  void PlayMenuVoice(int32_t index)override{audio_.PlayMenu(index);}
  bool ChannelPlaying(uint32_t channel)override{return audio_.Playing(channel);}
  void StopVoice(uint32_t channel)override{audio_.Stop(channel);}
  int32_t UpdateMenuNavigation(int32_t selected)override{
    const auto& nav=navigation_.Update(selected,NavigationInput);
    for(uint32_t i=0;i<nav.Sounds;++i)UiSound(2);
    if(!Input.Accept && !Input.Cancel)Input.ReleasedMenuButton=Contacts.UpdateMenu();
    return nav.Selected;
  }
  void BeginSubmenu(TitleSubmenu)override{throw UnsupportedCommand("PC title submenu owner is not bound");}
  int32_t UpdateSubmenu(TitleSubmenu)override{throw UnsupportedCommand("PC title submenu owner is not bound");}
  void DestroySubmenu(TitleSubmenu)override{throw UnsupportedCommand("PC title submenu owner is not bound");}
  void PrepareMovie()override{throw UnsupportedCommand("PC title OP1 movie owner is not bound");}
  bool MovieReady()override{throw UnsupportedCommand("PC title movie owner is not bound");}
  void StartMovie()override{throw UnsupportedCommand("PC title movie owner is not bound");}
  bool MovieBusy()override{throw UnsupportedCommand("PC title movie owner is not bound");}
  bool MovieFinished()override{throw UnsupportedCommand("PC title movie owner is not bound");}
  void StopMovie()override{throw UnsupportedCommand("PC title movie owner is not bound");}
  uint8_t MovieVolumeOption()const override{return owner_.SystemProgress->Bytes6022c[7];}
  void SetMovieVolume(int32_t)override{throw UnsupportedCommand("PC title movie owner is not bound");}
  void ResetEffects(int32_t variant)override{ResetTitleEffects(Effects,env_,variant,Now);}
  void UpdatePressStartVisual(uint64_t)override{Input.PressStartReleased=Contacts.UpdatePressStart();UpdateTitlePressStartVisual(Visual);}
  void UpdateEffects(uint64_t now)override{UpdateTitleEffects(Effects,env_,now);}
  void TickAmbient(uint64_t now)override{TickTitleAmbient(Visual,now);}
  void RequestScene(int32_t scene)override{
    if(RequestedScene)throw FormatError("title requested a second destination");
    RequestedScene=scene;
  }
  void Update(uint64_t now){Now=now;UpdateTitle(State,env_,*this,Input,now);}
  void Draw(FontTextures& textures,PcFontFace& font,uint32_t fontSize,int width,int height,
      std::ostream* sprites=nullptr){
    auto in=TitleDrawFromState(State,Visual.Blink[0],{Visual.Ambient[0].Current,Visual.Ambient[1].Current},fontSize);
    in.ResourceDrawBlocked=!committed_;
    TitleOverlayInputs overlay;overlay.Phase=State.Phase;overlay.FadeAlpha=State.Fade.Value.Current;
    overlay.MovieFadeAlpha=State.MovieFade.Value.Current;overlay.ResourceDrawBlocked=!committed_;
    const auto effects=[&](uint32_t id){
      if(id!=68)throw FormatError("unknown title effect atlas");
      for(const auto& sprite:BuildTitleEffects(Effects,env_)){
        renderer_.DrawQuad(sprite,renderer_.Textures.at(sprite.Texture),false);
        if(sprites){WriteUiBlit(*sprites,sprite);*sprites<<'\n';}
      }
    };
    const auto services=[](const TitleOverlayCommand& c){
      // Fresh title owns no active shared modal. Movie/submenus reject at entry.
      if(c.Kind!=TitleOverlayKind::SharedDialog)throw UnsupportedCommand("PC title foreground service is not bound");
    };
    DrawTitleFrame(renderer_,textures,font,env_,BuildTitleDraw(env_,in),BuildTitleOverlay(env_,overlay),width,height,effects,services,sprites);
  }
  void Retire(){
    audio_.Retire();
    for(const auto& key:keys_)if(const auto it=renderer_.Textures.find(key);it!=renderer_.Textures.end()){
      glDeleteTextures(1,&it->second.Handle);renderer_.Textures.erase(it);
    }
    keys_.clear();ready_=committed_=false;
  }
 private:
  SemanticState& owner_;GameResources& resources_;UiBlitRenderer& renderer_;MusicPlayback& playback_;
  ArchiveBgmResources& music_;TitleAudioOwner audio_;TitleEnvironment env_;TitleSessionSeeds seeds_;
  TitleNavigation navigation_;GameRandom random_;std::function<void()> clearHistory_;
  std::vector<ProfileTextureKey> keys_;bool ready_=false,committed_=false,firstMusic_=true;
};
