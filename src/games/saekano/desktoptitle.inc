// Included in desktop.cpp after the shared UI/font consumers. This owner binds
// the native title controller, rather than manufacturing a later story cursor.
class PcTitleOwner final:public TitleServices {
 public:
  TitleState State;
  TitleInput Input;
  TitleNavigationInput NavigationInput;
  OptionsPageInput OptionsInput;DialogInput OptionsModalInput;WidgetInputHistory::Snapshot FrameContacts{};
  size_t ConfigEntries=0,ConfigReturns=0,ConfigPreviews=0,ConfigQuestions=0,ConfigCancels=0,ConfigSaves=0;
  PcOptionsOwner* Config()const{return config_.get();}
  PcExtrasInput ExtrasInput;
  PcExtrasOwner* Extras()const{return extras_.get();}
  SlotBrowserInput SlotInput;
  PcSlotOwner* Continue()const{return slot_.get();}
  size_t ContinueEntries=0,ContinueReturns=0,ContinueDraws=0,ContinueCards=0;
  int32_t ContinueResult=0,ContinueSelected=-1;
  TitleVisualState Visual;
  TitleEffectState Effects;
  TitleWidgetInput Contacts;
  uint64_t Now=0;
  uint32_t MovieFlags=0;
  std::optional<int32_t> RequestedScene;
  std::unique_ptr<NativePendingSlotLoad> PendingLoad;
  size_t Resets=0,HistoryClears=0;
  PcTitleOwner(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,FontTextures& textures,PcFontFace& font,
      MusicPlayback& playback,ArchiveBgmResources& music,ArchiveSeResources& sounds,PlaybackTitleAudio& output,PlaybackVoiceAttenuation& attenuation,
      MoviePlayback& movie,PcMovieFrameLayer& movieFrame,ViewerWindow& window,const std::filesystem::path& root,
      TitleEnvironment environment,TitleSessionSeeds seeds,std::span<const int32_t> repeats,
      uint32_t holdFrames,std::function<void()> clearHistory,const std::filesystem::path& saveRoot,NativeSlotData& slots,
      std::function<uint64_t()> audioClock,uint64_t now)
      :Contacts(environment,holdFrames),Now(now),owner_(owner),resources_(resources),renderer_(renderer),
       playback_(playback),music_(music),audio_(resources,output),movie_(movie),movieFrame_(movieFrame),root_(root),env_(std::move(environment)),
       seeds_(std::move(seeds)),navigation_(repeats),clearHistory_(std::move(clearHistory)),textures_(textures),font_(font),sounds_(sounds),audioOutput_(output),attenuation_(attenuation),
       saveRoot_(saveRoot),slots_(slots),repeats_(repeats.begin(),repeats.end()),hold_(holdFrames),audioClock_(std::move(audioClock)),window_(window) {
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
      playback_.Submit(event);attenuation_.SetGain(int32_t(event.Bgm->Volume->ConvertedVolume));firstMusic_=false;
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
  void BeginSubmenu(TitleSubmenu kind)override{
    if(kind==TitleSubmenu::Load){
      if(slot_ || config_ || extras_)throw FormatError("another title submenu is active");
      slot_=std::make_unique<PcSlotOwner>(owner_,resources_,renderer_,textures_,font_,audioOutput_,root_,saveRoot_,slots_,repeats_,hold_,Now);
      slot_->BeginFrame(SlotInput,OptionsModalInput,FrameContacts,Now);++ContinueEntries;return;
    }
    if(kind==TitleSubmenu::Extras){
      if(slot_ || config_ || extras_)throw FormatError("another title submenu is active");
      extras_=std::make_unique<PcExtrasOwner>(owner_,resources_,renderer_,textures_,font_,audioOutput_,playback_,window_,music_,sounds_,root_,saveRoot_,slots_,repeats_,hold_,
        [this](bool active){PlayBgmAndMark(active?1:0);});
      extras_->BeginFrame(ExtrasInput,OptionsModalInput,FrameContacts,Now);return;
    }
    if(kind!=TitleSubmenu::Config)throw UnsupportedCommand("unknown PC title submenu");
    if(config_)throw FormatError("Options owner is already active");
    config_=std::make_unique<PcOptionsOwner>(owner_,resources_,renderer_,textures_,font_,playback_,audioOutput_,attenuation_,music_,sounds_,root_,saveRoot_,slots_,repeats_,hold_,audioClock_,
      [this](uint8_t mode){audio_.Load(mode);},[this](uint32_t id){if(trophies_.insert(id).second)SDL_Log("Saekano session trophy %u",id);},Now);
    config_->BeginFrame(OptionsInput,OptionsModalInput,FrameContacts,Now);++ConfigEntries;
  }
  int32_t UpdateSubmenu(TitleSubmenu kind)override{
    if(kind==TitleSubmenu::Load){if(!slot_)throw FormatError("Continue update has no owner");return slot_->Update(Now);}
    if(kind==TitleSubmenu::Extras){if(!extras_)throw FormatError("Extras update has no owner");return extras_->Update(Now);}
    if(kind!=TitleSubmenu::Config || !config_)throw UnsupportedCommand("PC submenu update has no owner");return config_->Update(Now);
  }
  void DestroySubmenu(TitleSubmenu kind)override{
    if(kind==TitleSubmenu::Extras){
      if(!extras_ || extras_->State().Phase()!=10)throw FormatError("Extras retired before completion");extras_.reset();return;
    }
    if(kind==TitleSubmenu::Load){
      if(!slot_ || !slot_->Result)throw FormatError("Continue retired before completion");
      ContinueResult=slot_->Result;ContinueSelected=slot_->Browser.Rows.Selected;ContinueDraws+=slot_->DrawFrames;ContinueCards+=slot_->CardCommands;
      slot_.reset();++ContinueReturns;return;
    }
    if(kind!=TitleSubmenu::Config || !config_ || !config_->State.Finished)throw FormatError("PC submenu retired before completion");
    ConfigPreviews+=config_->PreviewStarts;ConfigQuestions+=config_->QuestionBegins;ConfigCancels+=config_->QuestionCancels;ConfigSaves+=size_t(config_->Dialogs().Save.State().Accepted);
    config_.reset();++ConfigReturns;
  }
  void PrepareMovie()override{movieFrame_.Retire();movie_.Prepare(Profile::Saekano::AssetPath(root_, Profile::Saekano::GameAsset::OpeningMovie));}
  bool MovieReady()override{return movie_.Ready();}
  void StartMovie()override{movie_.Start(Now);}
  bool MovieBusy()override{return movie_.State().Prepared && !movie_.State().Finished;}
  bool MovieFinished()override{return movie_.State().Finished;}
  void StopMovie()override{movie_.Stop();movieFrame_.Retire();}
  uint8_t MovieVolumeOption()const override{return owner_.SystemProgress->Bytes6022c[7];}
  void SetMovieVolume(int32_t percent)override{movie_.SetVolume(percent);}
  void ResetEffects(int32_t variant)override{ResetTitleEffects(Effects,env_,variant,Now);}
  void UpdatePressStartVisual(uint64_t)override{Input.PressStartReleased=Contacts.UpdatePressStart();UpdateTitlePressStartVisual(Visual);}
  void UpdateEffects(uint64_t now)override{UpdateTitleEffects(Effects,env_,now);}
  void TickAmbient(uint64_t now)override{TickTitleAmbient(Visual,now);}
  void RequestScene(int32_t scene)override{
    if(RequestedScene)throw FormatError("title requested a second destination");
    if(slot_ && slot_->Result==3){
      if(scene!=2 || !slot_->PendingLoad)throw FormatError("Continue completed without pending scene data");
      ContinueSelected=slot_->Browser.Rows.Selected;ContinueResult=slot_->Result;
      ContinueDraws+=slot_->DrawFrames;ContinueCards+=slot_->CardCommands;
      PendingLoad=std::move(slot_->PendingLoad);
    }
    RequestedScene=scene;
  }
  void Update(uint64_t now){
    Now=now;if(config_)config_->BeginFrame(OptionsInput,OptionsModalInput,FrameContacts,now);
    if(slot_)slot_->BeginFrame(SlotInput,OptionsModalInput,FrameContacts,now);
    if(extras_)extras_->BeginFrame(ExtrasInput,OptionsModalInput,FrameContacts,now);
    movie_.Update(now);MovieFlags=uint32_t(movie_.Ready())|(uint32_t(movie_.State().Prepared && !movie_.State().Finished)<<1)|
      (uint32_t(movie_.State().Finished)<<2);UpdateTitle(State,env_,*this,Input,now);
  }
  void Draw(FontTextures& textures,PcFontFace& font,uint32_t fontSize,int width,int height,
      std::ostream* sprites=nullptr){
    auto in=TitleDrawFromState(State,Visual.Blink[0],{Visual.Ambient[0].Current,Visual.Ambient[1].Current},fontSize);
    in.ResourceDrawBlocked=!committed_;
    TitleOverlayInputs overlay;overlay.Phase=State.Phase;overlay.FadeAlpha=State.Fade.Value.Current;
    overlay.MovieFadeAlpha=State.MovieFade.Value.Current;overlay.ResourceDrawBlocked=!committed_;overlay.SubmenusPresent[1]=bool(config_);
    overlay.SubmenusPresent[0]=bool(slot_);
    overlay.SubmenusPresent[2]=bool(extras_);
    const auto effects=[&](uint32_t id){
      if(id!=68)throw FormatError("unknown title effect atlas");
      for(const auto& sprite:BuildTitleEffects(Effects,env_)){
        renderer_.DrawQuad(sprite,renderer_.Textures.at(sprite.Texture),false);
        if(sprites){WriteUiBlit(*sprites,sprite);*sprites<<'\n';}
      }
    };
    const auto services=[&](const TitleOverlayCommand& c){
      if(c.Kind==TitleOverlayKind::MovieChannel){
        if(c.Index>1)throw FormatError("title movie channel outside native pair");
        if(c.Index==0 && movie_.State().Started && !movie_.State().Finished)movieFrame_.Draw();
      }else if(c.Kind==TitleOverlayKind::Submenu && c.Index==0 && slot_){slot_->Draw();}
      else if(c.Kind==TitleOverlayKind::Submenu && c.Index==1 && config_){config_->Draw();}
      else if(c.Kind==TitleOverlayKind::Submenu && c.Index==2 && extras_){extras_->Draw();}
      else if(c.Kind!=TitleOverlayKind::SharedDialog)throw UnsupportedCommand("PC title foreground service is not bound");
    };
    DrawTitleFrame(renderer_,textures,font,env_,BuildTitleDraw(env_,in),BuildTitleOverlay(env_,overlay),width,height,effects,services,sprites);
  }
  void Retire(){
    extras_.reset();slot_.reset();config_.reset();
    movie_.Stop();movieFrame_.Retire();
    audio_.Retire();
    for(const auto& key:keys_)if(const auto it=renderer_.Textures.find(key);it!=renderer_.Textures.end()){
      Impacto::Renderer->FreeTexture(it->second.Handle);renderer_.Textures.erase(it);
    }
    keys_.clear();ready_=committed_=false;
  }
 private:
  SemanticState& owner_;GameResources& resources_;UiBlitRenderer& renderer_;MusicPlayback& playback_;
  ArchiveBgmResources& music_;TitleAudioOwner audio_;MoviePlayback& movie_;PcMovieFrameLayer& movieFrame_;std::filesystem::path root_;
  TitleEnvironment env_;TitleSessionSeeds seeds_;
  TitleNavigation navigation_;GameRandom random_;std::function<void()> clearHistory_;
  FontTextures& textures_;PcFontFace& font_;ArchiveSeResources& sounds_;PlaybackTitleAudio& audioOutput_;PlaybackVoiceAttenuation& attenuation_;
  std::filesystem::path saveRoot_;NativeSlotData& slots_;std::vector<int32_t> repeats_;uint32_t hold_;std::function<uint64_t()> audioClock_;
  std::unique_ptr<PcSlotOwner> slot_;
  ViewerWindow& window_;std::unique_ptr<PcExtrasOwner> extras_;
  std::unique_ptr<PcOptionsOwner> config_;std::set<uint32_t> trophies_;
  std::vector<ProfileTextureKey> keys_;bool ready_=false,committed_=false,firstMusic_=true;
};
