// Included after the shared modal contacts and drawing consumers.
class PcSlotOwner final:public SlotControllerServices {
 public:
  SlotControllerState State;
  SlotBrowserState Browser;
  SlotBrowserInput Input;
  DialogInput ModalInput;
  uint64_t Now=0;
  int32_t Result=0;
  bool BrowserUpdated=false;
  size_t DrawFrames=0,CardCommands=0,MarkerDraws=0,DialogBegins=0;
  std::unique_ptr<NativePendingSlotLoad> PendingLoad;
  const DialogState& Question() const { return question_; }
  PcSlotOwner(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,
      FontTextures& textures,PcFontFace& font,PlaybackTitleAudio& audio,
      const std::filesystem::path& root,const std::filesystem::path& saveRoot,NativeSlotData& slots,
      std::span<const int32_t> repeats,uint32_t hold,uint64_t now,
      int32_t mode=1,std::function<NativeSlotRecord()> workingRecord={})
      :Now(now),owner_(owner),renderer_(renderer),audio_(audio),slots_(slots),
       env_(SlotEnvironment::Decode(SceneRead(root/"env/env_saveload.dat"))),
       markerEnv_(NewMarkerEnvironment::Decode(SceneRead(root/"env/env_global.dat"))),
       marker_(StartNewMarker(markerEnv_,now)),
       format_(TextFormatEnvironment::Decode(SceneRead(root/"env/env_string_format.dat"),true)),
       shared_(renderer,textures,font,resources,format_,
         TextLayoutMetrics::Decode(SceneRead(root/"env/env_global.dat"),SceneRead(root/"env/env_message_window.dat")),
         GameUtilityFont::Decode(SceneRead(root/"env/env_game_utility.dat")).Size,
         DialogEnvironment::Decode(SceneRead(root/"env/env_dialog.dat")),SceneRead(root/"first.dat")),
       storage_(saveRoot,slots,[this]{return SnapshotSystemOwner(owner_,RestoreSystemReadHistory(RequireSystem()));}),
       save_(SaveManagerEnvironment::Decode(SceneRead(root/"env/env_save_manager.dat")),
         [this](const std::u16string& prompt,size_t count){return shared_.Layout(prompt,count);},storage_),
       widgets_(env_,hold),widgetServices_(widgets_,[this](int32_t id){UiSound(id);}),
       thumbnails_(renderer,slots.Records),
       help_(SlotButtonHelpEnvironment::Decode(SceneRead(root/"env/env_help_mgr.dat"))),
       helpStyle_(ButtonHelpStyle::Decode(SceneRead(root/"env/env_global.dat"))),hold_(hold),
       workingRecord_(std::move(workingRecord)){
    if(slots_.Records.size()!=60)throw FormatError("Continue needs all sixty native records");
    for(const auto& [key,texture]:renderer_.Textures){(void)texture;priorTextures_.insert(key);}
    const auto selected=RequireSystem().Word60318;
    if(selected>=60)throw FormatError("saved slot selection is outside native records");
    StartSlotBrowser(Browser,env_.BrowserSettings(),repeats,int32_t(selected),now);
    if(mode==0 && !workingRecord_)throw FormatError("scenario Save needs a working-record owner");
    StartSlotController(State,mode,env_.Scalars.at(0xc),now);
  }
  ~PcSlotOwner(){
    for(auto it=renderer_.Textures.begin();it!=renderer_.Textures.end();){
      if(!priorTextures_.contains(it->first)){glDeleteTextures(1,&it->second.Handle);it=renderer_.Textures.erase(it);}else ++it;
    }
  }
  void BeginFrame(const SlotBrowserInput& input,const DialogInput& modal,
      const WidgetInputHistory::Snapshot& contacts,uint64_t now){
    Input=input;ModalInput=modal;Now=now;BrowserUpdated=false;widgetServices_.BeginFrame(contacts,now);
    questionContacts_.BeginFrame(contacts);saveContacts_.BeginFrame(contacts);
  }
  void CancelPointer(){widgets_.CancelPointer();questionContacts_.CancelPointer();saveContacts_.CancelPointer();}
  int32_t Update(uint64_t now){Now=now;Result=UpdateSlotController(State,*this,now);return Result;}
  int32_t UpdateBrowser()override{
    std::array<uint8_t,60> occupied{};
    for(size_t i=0;i<60;++i)occupied[i]=slots_.Records[i].Bytes2b6[2];
    BrowserUpdated=true;browserSounds_.clear();collectBrowserSounds_=true;
    browserResult_=UpdateSlotBrowser(Browser,Input,widgetServices_,occupied,State.Mode,Now);
    collectBrowserSounds_=false;return browserResult_;
  }
  void UiSound(int32_t id)override{if(collectBrowserSounds_)browserSounds_.push_back(id);audio_.PlayUi(id);}
  void WriteBrowserSnapshot(std::ostream& out)const{
    const auto& r=Browser.Rows;
    out<<"{\"result\":"<<browserResult_<<",\"rows\":["<<r.Scroll<<','<<r.Selected<<','<<r.PreviousScroll<<','<<r.PreviousSelected
      <<"],\"scrolling\":"<<Browser.Scrolling<<",\"any\":"<<Browser.Navigation.Any<<",\"channels\":[["<<Browser.Scroll.Current<<','<<Browser.Scroll.Elapsed
      <<"],["<<Browser.Cursor.Current<<','<<Browser.Cursor.Elapsed<<"]],\"sounds\":[";
    for(size_t i=0;i<browserSounds_.size();++i){if(i)out<<',';out<<browserSounds_[i];}out<<"],\"widgets\":[";
    bool comma=false;
    for(int kind=0;kind<4;++kind)for(const auto& w:widgets_.Records())if(int(w.Kind)==kind){
      if(comma)out<<',';comma=true;out<<'['<<kind<<','<<w.Row;
      for(const auto& c:w.Components)out<<','<<c.Body.Input.Flags<<','<<c.Body.Input.TrackedId;out<<']';
    }
    out<<"]}";
  }
  bool SelectedOccupied()const override{return Selected().Bytes2b6[2]!=0;}
  bool SelectedProtected()const override{return Selected().Bytes2b6[1]!=0;}
  void SetSelectedProtected(bool value)override{slots_.Records.at(size_t(Browser.Rows.Selected)).Bytes2b6[1]=uint8_t(value);}
  void BeginDialog(SlotPrompt prompt)override{
    uint16_t at=0;questionGroup_=1;
    switch(prompt){
      case SlotPrompt::Load:at=0x6a8;break;
      case SlotPrompt::Loading:at=0x6e0;questionGroup_=0;break;
      case SlotPrompt::Protect:at=0x788;break;
      case SlotPrompt::Unprotect:at=0x7c0;break;
    }
    prompt_=env_.Strings.at(at);layout_=shared_.Layout(prompt_,shared_.Environment.ButtonGroups.at(questionGroup_).size());
    Saekano::BeginDialog(question_,layout_,shared_.Environment.ButtonGroups.at(questionGroup_).size(),
        0,prompt!=SlotPrompt::Loading,true,Now);
    questionOwned_=true;++DialogBegins;
  }
  SlotDialogPoll UpdateDialog()override{
    if(!questionOwned_)throw FormatError("Continue modal has no owner");
    auto input=ModalInput;input.CollectActivations=[this]{return questionContacts_.Collect(question_,shared_.Environment,hold_,Now);};
    Saekano::UpdateDialog(question_,input,Now);
    for(const auto id:question_.UiSounds)UiSound(id);
    return {question_.Visible,question_.Canceled,question_.Result};
  }
  void RetireDialog()override{question_={};questionOwned_=false;}
  void StageSelectedLoad()override{
    if(PendingLoad)throw FormatError("Continue staged a second slot load");
    PendingLoad=StageNativeSlotLoad(slots_,size_t(Browser.Rows.Selected));
  }
  void BeginSlotSave()override {
    if(!workingRecord_ || State.Mode!=0)throw FormatError("slot Save has no scenario owner");
    slotSavePhase_=0;saveBackup_.reset();save_.Begin(2,Now);
  }
  bool UpdateSlotSave()override {
    // Original5c562. Existing services own paired I/O and worker dialogs;
    // this owner handles confirmation, record replacement and rollback.
    if(slotSavePhase_==0) {
      if(SelectedProtected()) {
        OpenSaveQuestion(0x7f8,0);slotSavePhase_=5;
      }else {
        saveBackup_=Selected();
        OpenSaveQuestion(SelectedOccupied()?0x600:0x5c8,1);slotSavePhase_=1;
      }
    }
    if(slotSavePhase_==1) {
      const auto q=UpdateDialog();
      if(!q.Visible) {
        RetireDialog();
        if(q.Selection!=1 || q.Cancelled){saveBackup_.reset();return true;}
        if(!owner_.TitleSession)throw FormatError("slot Save has no title-session aliases");
        const auto stamp=std::time(nullptr);std::tm local{};
#ifdef _WIN32
        if(localtime_s(&local,&stamp)!=0)throw FormatError("slot Save clock is unavailable");
#else
        if(!localtime_r(&stamp,&local))throw FormatError("slot Save clock is unavailable");
#endif
        owner_.TitleSession->Words={local.tm_year+1900,local.tm_mon+1,local.tm_mday,
                                   local.tm_hour,local.tm_min,local.tm_sec};
        auto working=workingRecord_();
        if(!working.Bytes2b6[2])throw FormatError("slot Save working record is unoccupied");
        for(size_t i=0;i<slots_.Records.size();++i)oldNewFlags_[i]=slots_.Records[i].Bytes2b6[0];
        oldSlotSelection_=RequireSystem().Word60318;
        const auto index=size_t(Browser.Rows.Selected);
        // Serialized byte0 is NEW; byte2 is occupancy. Never clear occupancy.
        working.Bytes2b6[0]=1;
        for(auto& record:slots_.Records)record.Bytes2b6[0]=0;
        owner_.TitleSession->Flags[2]=1;
        slots_.Records[index]=std::move(working);
        RequireSystem().Word60318=uint32_t(index);
        thumbnails_.Replace(uint32_t(index),slots_.Records[index].Thumbnail);
        slotSavePhase_=2;
      }
    }
    if(slotSavePhase_==2) {
      const auto poll=UpdateSystemSave();
      if(!poll.Busy) {
        if(!poll.Accepted) {
          const auto index=size_t(Browser.Rows.Selected);
          slots_.Records[index]=std::move(*saveBackup_);
          for(size_t i=0;i<slots_.Records.size();++i)slots_.Records[i].Bytes2b6[0]=oldNewFlags_[i];
          RequireSystem().Word60318=oldSlotSelection_;
          thumbnails_.Replace(uint32_t(index),slots_.Records[index].Thumbnail);
        }
        saveBackup_.reset();return true;
      }
    }
    if(slotSavePhase_==5 && !UpdateDialog().Visible){RetireDialog();return true;}
    return false;
  }
  void BeginSystemSave()override{save_.Begin(2,Now);}
  SlotWritePoll UpdateSystemSave()override{
    save_.Input=ModalInput;save_.Input.CollectActivations=[this]{return saveContacts_.Collect(save_.Modal,shared_.Environment,hold_,Now);};
    const auto poll=save_.Update(Now);for(const auto id:save_.Modal.UiSounds)UiSound(id);
    return {poll.Active,poll.Accepted};
  }
  void TickNewMarker()override{UpdateNewMarker(marker_,markerEnv_,Now);}
  void Draw(){
    const auto text=[&](const UiTextBlit& t){
      shared_.Textures.DrawString(renderer_,shared_.Font,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);
    };
    const auto primitive=[&](const UiRenderCommand& c){
      if(c.Kind==UiRenderKind::Sprite)shared_.Sprite(c.Sprite);
      else renderer_.Execute(std::span<const UiRenderCommand>(&c,1));
    };
    const auto input=SlotDrawFromState(State,Browser,true,questionOwned_,{help_.Rectangle[0],help_.Rectangle[1]});
    for(const auto& c:BuildSlotDraw(env_,input))switch(c.Kind){
      case SlotDrawKind::Sprite:shared_.Sprite(c.Sprite);break;
      case SlotDrawKind::Rectangle:{UiRenderCommand r;r.Kind=UiRenderKind::Rectangle;r.Rectangle=c.Rectangle;r.Color=c.Color;primitive(r);break;}
      case SlotDrawKind::Widget:for(const auto& w:widgets_.Draw(c.Widget,c.Index,c.Color))primitive(w);break;
      case SlotDrawKind::Browser:
        for(const auto& v:BuildSlotViewport(SlotViewportFromState(env_,Browser))){
          if(v.Kind==SlotViewportKind::Render)primitive(v.Render);
          else if(v.Kind==SlotViewportKind::Cards){
            for(const auto& card:BuildSlotCards(env_,slots_.Records,State.Mode,v.Cards,
                uint8_t(Browser.Highlight.Channel.Current),shared_.Font.Cached(),uint32_t(shared_.FontSize),shared_.Font.NominalSize())){
              ++CardCommands;
              if(card.Kind==SlotCardDrawKind::Sprite)shared_.Sprite(card.Sprite);
              else if(card.Kind==SlotCardDrawKind::Text)text(card.Text);
              else{for(const auto& s:BuildNewMarker(marker_,markerEnv_,{ProfileTextureSection::SharedUi,-1,18},card.Position))shared_.Sprite(s);++MarkerDraws;}
            }
          }else throw UnsupportedCommand("static Continue viewport has no authored consumer");
        }
        break;
      case SlotDrawKind::Decoration:
        for(const auto& t:BuildButtonHelp(help_.Rows.at(c.Index),help_.Rectangle,helpStyle_,
            shared_.Font.Cached(),uint32_t(shared_.FontSize),shared_.Font.NominalSize()))text(t);
        break;
      case SlotDrawKind::Question:shared_.Draw(question_,prompt_,layout_,questionGroup_,c.Color);break;
      case SlotDrawKind::Save:shared_.Draw(save_.Modal,save_.Prompt(),save_.Layout(),0,c.Color);break;
    }
    questionContacts_.Latch(question_,layout_,shared_.Environment);
    saveContacts_.Latch(save_.Modal,save_.Layout(),shared_.Environment);
    GlCheck("Continue frame composition");++DrawFrames;
  }
  bool QuestionVisible()const{return question_.Visible;}
 private:
  void OpenSaveQuestion(uint16_t text,size_t group) {
    prompt_=env_.Strings.at(text);questionGroup_=group;
    const auto count=shared_.Environment.ButtonGroups.at(group).size();
    layout_=shared_.Layout(prompt_,count);
    Saekano::BeginDialog(question_,layout_,count,0,true,true,Now);questionOwned_=true;++DialogBegins;
  }
  NativeSystemSave& RequireSystem(){if(!owner_.SystemProgress)throw FormatError("Continue system owner is missing");return *owner_.SystemProgress;}
  const NativeSlotRecord& Selected()const{return slots_.Records.at(size_t(Browser.Rows.Selected));}
  SemanticState& owner_;UiBlitRenderer& renderer_;PlaybackTitleAudio& audio_;NativeSlotData& slots_;
  SlotEnvironment env_;NewMarkerEnvironment markerEnv_;NewMarkerState marker_;TextFormatEnvironment format_;
  SceneDialogLayer shared_;SessionSaveWrites storage_;SaveManagerDialogHost save_;
  SlotWidgets widgets_;SlotWidgetServices widgetServices_;PcSlotThumbnails thumbnails_;
  SlotButtonHelpEnvironment help_;ButtonHelpStyle helpStyle_;uint32_t hold_;
  DialogState question_;bool questionOwned_=false;size_t questionGroup_=1;
  std::u16string prompt_;DialogContentLayout layout_;
  PcDialogContacts questionContacts_,saveContacts_;std::set<ProfileTextureKey> priorTextures_;
  int32_t browserResult_=3;bool collectBrowserSounds_=false;std::vector<int32_t> browserSounds_;
  std::function<NativeSlotRecord()> workingRecord_;
  int32_t slotSavePhase_=0;std::optional<NativeSlotRecord> saveBackup_;
  std::array<uint8_t,NativeSlotData::SlotCount> oldNewFlags_{};uint32_t oldSlotSelection_=0;
};
