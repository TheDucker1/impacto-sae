struct PcExtrasInput {
  OptionsNavigationInput Vertical,Horizontal,Pages;
  bool Accept=false,Cancel=false,Comment=false,ToggleHelp=false,Stop=false,Mode=false;
};
// Owns actual children and the shared target/modal/save presentation. Title
// lends its committed title/Extras atlas, persistent owner and physical services.
class PcExtrasOwner final:public ExtrasServices {
 public:
  PcExtrasOwner(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,
      FontTextures& textures,PcFontFace& font,PlaybackTitleAudio& audio,MusicPlayback& playback,
      ViewerWindow& window,ArchiveBgmResources& music,ArchiveSeResources& sounds,
      const std::filesystem::path& root,const std::filesystem::path& saveRoot,NativeSlotData& slots,
      std::span<const int32_t> repeats,uint32_t hold,std::function<void(bool)> switchAudio)
      :owner_(owner),audio_(audio),playback_(playback),window_(window),music_(music),sounds_(sounds),
       path_(root),saveRoot_(saveRoot),slots_(slots),repeats_(repeats.begin(),repeats.end()),hold_(hold),
       debug_(OptionsDebugSettings::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::DebugEnvironment))),
       presentation_(owner,resources,renderer,textures,font,audio,root,saveRoot,slots,hold,std::move(switchAudio)),
       root_(AlbumEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::ExtrasEnvironment)),repeats,hold,owner.MusicHistory,*this){}
  ~PcExtrasOwner(){DestroyChild();}
  void BeginFrame(const PcExtrasInput& input,const DialogInput& modal,const WidgetInputHistory::Snapshot& contacts,uint64_t now){
    input_=input;modal_=modal;contacts_=contacts;now_=now;root_.BeginFrame(contacts);presentation_.BeginFrame(modal,contacts,now);
  }
  void CancelPointer(){root_.CancelPointer();presentation_.CancelPointer();
    if(cg_)cg_->CancelPointer();if(movie_)movie_->CancelPointer();if(musicChild_)musicChild_->CancelPointer();if(battle_)battle_->CancelPointer();}
  int32_t Update(uint64_t now){now_=now;return root_.Update({input_.Vertical,input_.Accept,input_.Cancel},now);}
  void Draw(){presentation_.Draw(root_,[this]{
    if(cg_)cg_->Draw();else if(movie_)movie_->Draw();else if(musicChild_)musicChild_->Draw();else if(battle_)battle_->Draw();
    else throw FormatError("Extras draw lost its actual child");
  });}
  const ExtrasRoot& State()const{return root_;}
  const PcExtrasMusic* MusicChild()const{return musicChild_.get();}
  const PcExtrasCg* CgChild()const{return cg_.get();}
  const PcExtrasMovie* MovieChild()const{return movie_.get();}
  PcExtrasBattle* BattleChild()const{return battle_.get();}
  const DialogState& Question()const{return presentation_.Question();}
  const SaveManagerDialogHost& Save()const{return presentation_.Save();}
  bool WindowClosed()const{return windowClosed_;}
  bool ConsumeInputReset(){const bool result=inputReset_;inputReset_=false;return result;}
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  void SetExtrasAudio(bool active)override{presentation_.SetExtrasAudio(active);}
  void BeginChild(ExtrasSection section)override{
    if(cg_ || movie_ || musicChild_ || battle_)throw FormatError("Extras already owns a child");
    auto& ui=presentation_.Ui();const auto restore=[this]{presentation_.SetExtrasAudio(true);};
    switch(section){
      case ExtrasSection::Album:cg_=std::make_unique<PcExtrasCg>(ui,owner_,audio_,playback_,path_,repeats_,hold_,debug_.UnlockCg!=0,owner_.CommentaryHelpAvailable,now_);break;
      case ExtrasSection::Movies:movie_=std::make_unique<PcExtrasMovie>(ui,owner_,audio_,playback_,path_,repeats_,hold_,debug_.UnlockMovies!=0,restore,now_);break;
      case ExtrasSection::Music:musicChild_=std::make_unique<PcExtrasMusic>(ui,owner_,audio_,playback_,path_,restore,repeats_,hold_,debug_.UnlockMusic!=0,now_);break;
      case ExtrasSection::Battle:battle_=std::make_unique<PcExtrasBattle>(ui,owner_,audio_,playback_,window_,music_,sounds_,path_,saveRoot_,slots_,restore,repeats_,hold_,debug_.UnlockBattle!=0,now_);break;
    }
  }
  int32_t UpdateChild()override{
    if(cg_){ExtrasCgInput in;in.Categories=input_.Vertical;in.Pages=input_.Pages;in.Entries=input_.Horizontal;
      in.Grid={input_.Vertical.Pressed[0],input_.Vertical.Pressed[1],input_.Horizontal.Pressed[0],input_.Horizontal.Pressed[1]};
      in.Accept=input_.Accept;in.Cancel=input_.Cancel;in.Comment=input_.Comment;in.ToggleHelp=input_.ToggleHelp;
      cg_->BeginFrame(in,contacts_);return cg_->Update(now_);}
    if(movie_){movie_->BeginFrame({input_.Horizontal,input_.Accept,input_.Cancel},contacts_,now_);return movie_->Update(now_);}
    if(musicChild_){musicChild_->BeginFrame({input_.Vertical,input_.Accept,input_.Cancel,input_.Stop,input_.Mode},contacts_);return musicChild_->Update(now_);}
    if(battle_){battle_->BeginFrame({input_.Vertical,input_.Accept,input_.Cancel},modal_,contacts_);
      const auto serial=battle_->SessionSerial();const auto result=battle_->Update(now_);windowClosed_=battle_->WindowClosed();
      if(serial!=battle_->SessionSerial())inputReset_=true;return result;}
    throw FormatError("Extras update lost its actual child");
  }
  bool ChildDirty()const override{
    if(cg_)return cg_->Dirty();if(movie_)return movie_->Dirty();if(musicChild_)return musicChild_->Dirty();if(battle_)return battle_->Dirty();
    throw FormatError("Extras history queried after child retirement");
  }
  void DestroyChild()noexcept override{cg_.reset();movie_.reset();musicChild_.reset();battle_.reset();}
  void BeginSaveQuestion(const std::u16string& prompt)override{presentation_.BeginSaveQuestion(prompt);}
  OptionsDialogPoll UpdateSaveQuestion()override{return presentation_.UpdateSaveQuestion();}
  void BeginSystemSave()override{presentation_.BeginSystemSave();}
  OptionsSavePoll UpdateSystemSave()override{return presentation_.UpdateSystemSave();}
 private:
  SemanticState& owner_;PlaybackTitleAudio& audio_;MusicPlayback& playback_;ViewerWindow& window_;
  ArchiveBgmResources& music_;ArchiveSeResources& sounds_;std::filesystem::path path_,saveRoot_;NativeSlotData& slots_;
  std::vector<int32_t> repeats_;uint32_t hold_;OptionsDebugSettings debug_;PcExtrasPresentation presentation_;
  std::unique_ptr<PcExtrasCg> cg_;std::unique_ptr<PcExtrasMovie> movie_;std::unique_ptr<PcExtrasMusic> musicChild_;std::unique_ptr<PcExtrasBattle> battle_;
  PcExtrasInput input_;DialogInput modal_;WidgetInputHistory::Snapshot contacts_{};uint64_t now_=0;
  bool windowClosed_=false,inputReset_=false;ExtrasRoot root_;
};
