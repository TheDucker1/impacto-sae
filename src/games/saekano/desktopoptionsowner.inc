// SDL devices publish the same game actions; owners retain their own priority.
struct PcActionInput {
  bool Left=false,Right=false,Up=false,Down=false,Accept=false,Cancel=false;
  explicit PcActionInput(const SDL_Event& event) {
    if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
      const auto key=event.key.key;
      Left=key==SDLK_LEFT;Right=key==SDLK_RIGHT;Up=key==SDLK_UP;Down=key==SDLK_DOWN;
      Accept=key==SDLK_RETURN;
      Cancel=key==SDLK_ESCAPE || key==SDLK_BACKSPACE;
    } else if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
      const auto button=event.gbutton.button;
      Left=button==SDL_GAMEPAD_BUTTON_DPAD_LEFT;Right=button==SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
      Up=button==SDL_GAMEPAD_BUTTON_DPAD_UP;Down=button==SDL_GAMEPAD_BUTTON_DPAD_DOWN;
      Accept=button==SDL_GAMEPAD_BUTTON_EAST;Cancel=button==SDL_GAMEPAD_BUTTON_SOUTH;
    }
  }
  void Apply(DialogInput& input)const {
    input.Left|=Left;input.Right|=Right;input.Accept|=Accept;input.Cancel|=Cancel;
  }
};
// Concrete rectangle contacts, collected only when UpdateDialog reaches the
// native pointer branch. Geometry is latched by drawing, as native2a4/324 are.
class PcDialogContacts {
 public:
  void BeginFrame(const WidgetInputHistory::Snapshot& snapshot){input_.BeginFrame(snapshot);}
  void CancelPointer(){input_.Current=input_.Previous={};for(auto& b:buttons_){b.Input.Flags=0;b.Input.TrackedId=-1;}}
  void Latch(const DialogState& state,const DialogContentLayout& layout,const DialogEnvironment& e){
    if(!state.Visible)return;geometryEpoch_=state.Opacity.Epoch;
    for(size_t i=0;i<state.ButtonCount;++i){const auto& p=e.ButtonPositions.at(state.ButtonCount-1).at(i);
      positions_[i]={480-layout.Width/2+p[0]-e.TextExtent[0]/2,layout.ButtonBaseline+p[1]-e.TextExtent[1]/2};}
  }
  std::array<bool,3> Collect(const DialogState& state,const DialogEnvironment& e,uint32_t hold,uint64_t now){
    if(!buttonEpoch_ || *buttonEpoch_!=state.Opacity.Epoch){
      buttons_.clear();UiSpriteDefinition shape;shape.Width=e.TextExtent[0];shape.Height=e.TextExtent[1];
      for(size_t i=0;i<3;++i)buttons_.emplace_back(shape,ProfileTextureKey{ProfileTextureSection::SharedUi,-1,0},1.0f,true,hold);
      buttonEpoch_=state.Opacity.Epoch;
    }
    std::array<bool,3> result{};
    if(!geometryEpoch_ || *geometryEpoch_!=state.Opacity.Epoch)return result;
    for(size_t i=0;i<state.ButtonCount;++i){auto& b=buttons_[i];const auto& p=positions_[i];
      const auto contact=SampleWidgetContact(b.HitRectangle(p),input_.Current[0],input_.Previous[0],b.Input.TrackedId,input_.Velocity(0));
      b.Update(p,contact,now);input_.Publish(b.Input.Flags);
    }
    for(size_t i=0;i<3;++i)result[i]=buttons_[i].Input.Enabled && (buttons_[i].Input.Flags&8)!=0;
    return result;
  }
 private:
  WidgetInputHistory input_;std::vector<BattleCardSprite> buttons_;
  std::array<std::array<int32_t,2>,3> positions_{};std::optional<uint64_t> buttonEpoch_,geometryEpoch_;
};
class PcOptionsOwner final:public OptionsOwnerOutput {
 public:
  OptionsOwnerState State;
  OptionsPageInput Input;DialogInput ModalInput;
  uint64_t Now=0;size_t PreviewStarts=0,QuestionBegins=0,QuestionCancels=0;
  PcOptionsOwner(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,
      FontTextures& textures,PcFontFace& font,MusicPlayback& playback,PlaybackTitleAudio& audio,
      PlaybackVoiceAttenuation& attenuation,ArchiveBgmResources& music,ArchiveSeResources& sounds,
      const std::filesystem::path& root,const std::filesystem::path& saveRoot,const NativeSlotData& slots,
      std::span<const int32_t> repeats,uint32_t hold,std::function<uint64_t()> audioClock,
      std::function<void(uint8_t)> reload,std::function<void(uint32_t)> trophy,uint64_t now)
      :Now(now),owner_(owner),renderer_(renderer),playback_(playback),audio_(audio),attenuation_(attenuation),music_(music),sounds_(sounds),
       env_(OptionsEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::OptionsEnvironment))),
       debug_(OptionsDebugSettings::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::DebugEnvironment))),
       format_(TextFormatEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::StringFormatEnvironment),true)),
       shared_(renderer,textures,font,resources,format_,TextLayoutMetrics::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment),ReadGameFile(root, Profile::Saekano::GameAsset::MessageEnvironment)),
         GameUtilityFont::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::UtilityEnvironment)).Size,DialogEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::DialogEnvironment)),ReadGameFile(root, Profile::Saekano::GameAsset::FirstArchive)),
       storage_(saveRoot,slots,[this]{return Snapshot();}),
       dialogs_(shared_.Environment,SaveManagerEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::SaveManagerEnvironment)),
         [this](const std::u16string& prompt,size_t count){return shared_.Layout(prompt,count);},storage_),
       widgets_(env_,ReadOptions(RequireSystem()),Header(),hold),
       layer_(shared_,env_,ButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment)),ButtonHelpStyle::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment)),uint32_t(shared_.FontSize)),
       previewOutput_(*this),previews_(resources,previewOutput_),hold_(hold),audioClock_(std::move(audioClock)),reload_(std::move(reload)),trophy_(std::move(trophy)){
    if(!audioClock_ || !reload_ || !trophy_)throw FormatError("Options service is unavailable");
    for(const auto& [key,texture]:renderer_.Textures){(void)texture;priorTextures_.insert(key);}
    StartOptionsOwner(State,RequireSystem(),env_,repeats,GlobalUiEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment)).FadeDuration,Header(),now);
  }
  ~PcOptionsOwner(){
    try{playback_.StopOrdinaryVoice(7);}catch(...){}
    for(auto i=renderer_.Textures.begin();i!=renderer_.Textures.end();){
      if(!priorTextures_.contains(i->first)){glDeleteTextures(1,&i->second.Handle);i=renderer_.Textures.erase(i);}else ++i;
    }
  }
  void BeginFrame(const OptionsPageInput& input,const DialogInput& modal,const WidgetInputHistory::Snapshot& snapshot,uint64_t now){
    Input=input;ModalInput=modal;Now=now;widgets_.BeginFrame(snapshot);questionContacts_.BeginFrame(snapshot);saveContacts_.BeginFrame(snapshot);
  }
  void CancelPointer(){widgets_.CancelPointer();questionContacts_.CancelPointer();saveContacts_.CancelPointer();}
  int32_t Update(uint64_t now){Now=now;return UpdateOptionsOwner(State,RequireSystem(),Input,*this,now);}
  void Draw(){
    layer_.Draw(State,widgets_,Header(),dialogs_);
    questionContacts_.Latch(dialogs_.Question,dialogs_.Layout(),shared_.Environment);
    saveContacts_.Latch(dialogs_.Save.Modal,dialogs_.Save.Layout(),shared_.Environment);
  }
  OptionsDialogHost& Dialogs(){return dialogs_;}
  const OptionsEnvironment& Environment()const{return env_;}
  std::array<int32_t,4> QuestionButtonRectangle(size_t index)const{
    const auto& layout=dialogs_.Layout();const auto& e=shared_.Environment;const auto& p=e.ButtonPositions.at(dialogs_.Question.ButtonCount-1).at(index);
    return {480-layout.Width/2+p[0]-e.TextExtent[0]/2,layout.ButtonBaseline+p[1]-e.TextExtent[1]/2,e.TextExtent[0],e.TextExtent[1]};
  }
  void UpdateWidget(OptionsWidgetId id,int32_t x,int32_t y)override{widgets_.Update(id,x,y,Now);}
  std::optional<OptionsWidgetContact> CollectWidget(OptionsWidgetId id,int32_t x,int32_t y,uint64_t now)override{widgets_.Update(id,x,y,now);return widgets_.Contact(id);}
  std::optional<OptionsWidgetContact> QueryWidgetContact(OptionsWidgetId id)override{return widgets_.Contact(id);}
  void SetToggleVariant(uint32_t page,uint32_t index,bool inverted)override{widgets_.SetToggleVariant(page,index,inverted);}
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  void StopChannel(uint32_t ch)override{if(ch==6)playback_.StopUiSound();else playback_.StopOrdinaryVoice(ch);}
  void SetBgmVolume(int32_t percent)override{
    if(!owner_.BgmCommandVolume)throw UnsupportedCommand("Options BGM command gain is unknown");
    const auto command=*owner_.BgmCommandVolume;
    const auto request=BgmRequest(0,percent,command,music_.Coefficient());
    attenuation_.SetGain(int32_t(request.Volume->ConvertedVolume));owner_.BgmMasterVolume=percent;
  }
  void SetSeVolume(int32_t percent)override{
    if(!owner_.SeCommandVolume)throw UnsupportedCommand("Options SE command gain is unknown");
    const auto gain=SeVolume(percent,*owner_.SeCommandVolume,sounds_.Coefficient()).Gain;
    for(uint32_t ch=2;ch<=5;++ch)playback_.SetChannelOutputGain(ch,gain);owner_.SeMasterVolume=percent;
  }
  void SetUiVolume(int32_t percent)override{audio_.SetGains(float(RequireSystem().Bytes6022c[10])/10,float(percent)/100);}
  void SetChannelVolume(uint32_t ch,int32_t percent)override{
    playback_.SetChannelOutputGain(ch,float(percent)/100);
    if(ch==7)audio_.SetGains(float(percent)/100,float(RequireSystem().Bytes6022c[9])/10);
  }
  void SetDuckingVolume(int32_t percent)override{attenuation_.SetPercent(percent);}
  void PrepareSystemPreview(uint32_t ch,int32_t mode)override{if(ch!=7)throw FormatError("Options preview outside7");previews_.Prepare(mode);}
  void StartSystemPreview(uint32_t ch)override{if(ch!=7)throw FormatError("Options preview outside7");previews_.Start(audioClock_());++PreviewStarts;}
  int32_t UpdateSampleNavigation(int32_t)override{throw UnsupportedCommand("Options text-sample navigation is not bound");}
  void ResetSamplePage()override{throw UnsupportedCommand("Options text-sample owner is not bound");}
  void BeginSaveQuestion(const std::u16string& prompt)override{dialogs_.BeginQuestion(prompt,Now);++QuestionBegins;}
  OptionsDialogPoll UpdateSaveQuestion()override{
    dialogs_.Input=ModalInput;dialogs_.Input.CollectActivations=[this]{return questionContacts_.Collect(dialogs_.Question,shared_.Environment,hold_,Now);};
    const auto result=dialogs_.UpdateQuestion(Now);for(auto id:dialogs_.Question.UiSounds)UiSound(id);
    if(!result.Busy && result.Cancelled)++QuestionCancels;return result;
  }
  void BeginSystemSave()override{dialogs_.BeginSave(Now);}
  OptionsSavePoll UpdateSystemSave()override{
    dialogs_.Save.Input=ModalInput;dialogs_.Save.Input.CollectActivations=[this]{return saveContacts_.Collect(dialogs_.Save.Modal,shared_.Environment,hold_,Now);};
    const auto result=dialogs_.UpdateSave(Now);for(auto id:dialogs_.Save.Modal.UiSounds)UiSound(id);return result;
  }
  void BeginVoiceReload(uint8_t mode)override{StopChannel(7);reloadReady_=false;reload_(mode);reloadReady_=true;}
  bool VoiceReloadReady()override{return reloadReady_;}
  bool ForceSilent()const override{return debug_.ForceSilent();}
  void ApplyAudioOptions()override{
    const auto& option=RequireSystem().Bytes6022c;SetBgmVolume(int32_t(option[7])*10);SetSeVolume(int32_t(option[8])*10);SetUiVolume(int32_t(option[9])*10);
    for(uint32_t ch=7;ch<=12;++ch)SetChannelVolume(ch,int32_t(option[10])*10);SetDuckingVolume(int32_t(option[11])*10);
    ApplySystemPreferences(owner_,RequireSystem());
  }
  void RequestTrophy(uint32_t id)override{trophy_(id);}
  void TickDecoration(uint64_t)override{} // Footer selection/highlight are owned by OptionsOwner.
 private:
  NativeSystemSave& RequireSystem(){if(!owner_.SystemProgress)throw UnsupportedCommand("Options system owner is missing");return *owner_.SystemProgress;}
  bool Header()const{if(!owner_.OptionsHeaderByte)throw UnsupportedCommand("Options runtime header byte is unknown");return *owner_.OptionsHeaderByte!=0;}
  NativeSystemSave Snapshot(){
    auto result=SnapshotSystemOwner(owner_,RestoreSystemReadHistory(RequireSystem()));StoreOptions(result,ReadOptions(RequireSystem()));return result;
  }
  struct PreviewOutput final:ProfileVoiceOutput {
    PcOptionsOwner& P;explicit PreviewOutput(PcOptionsOwner& p):P(p){}
    void Reset()override{P.playback_.ResetProfileVoice();}
    void Prepare(Bytes bytes)override{P.playback_.PrepareProfileVoice(std::move(bytes),float(P.RequireSystem().Bytes6022c[10])/10);}
    void Start(uint64_t now)override{P.attenuation_.StartProfileVoice(now);}
    void Stop()override{P.playback_.StopProfileVoice();}
    bool Stopped()const override{return P.playback_.ProfileVoiceStopped();}
    void Update(uint64_t)override{}
  };
  SemanticState& owner_;UiBlitRenderer& renderer_;MusicPlayback& playback_;PlaybackTitleAudio& audio_;PlaybackVoiceAttenuation& attenuation_;
  ArchiveBgmResources& music_;ArchiveSeResources& sounds_;OptionsEnvironment env_;OptionsDebugSettings debug_;TextFormatEnvironment format_;
  SceneDialogLayer shared_;SessionSaveWrites storage_;OptionsDialogHost dialogs_;OptionsWidgets widgets_;PcOptionsLayer layer_;
  PreviewOutput previewOutput_;OptionsPreviewVoices previews_;uint32_t hold_;std::function<uint64_t()> audioClock_;
  std::function<void(uint8_t)> reload_;std::function<void(uint32_t)> trophy_;bool reloadReady_=false;
  PcDialogContacts questionContacts_,saveContacts_;std::set<ProfileTextureKey> priorTextures_;
};
