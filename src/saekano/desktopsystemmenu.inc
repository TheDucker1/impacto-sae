// Included after SceneDialogLayer, SessionSaveWrites and PcDialogContacts.
// The scenario wrapper owns entry/suspension and consumes the returned action.
class PcSystemMenuOwner final : public SystemMenuServices {
 public:
  DialogInput ModalInput;
  uint64_t Now=0;
  PcSystemMenuOwner(SemanticState& owner, GameResources& resources,
      UiBlitRenderer& renderer, FontTextures& textures, PcFontFace& font,
      PlaybackTitleAudio& audio, const std::filesystem::path& root,
      const std::filesystem::path& saveRoot, const NativeSlotData& slots,
      uint32_t hold,int32_t initialSelection=0)
      :owner_(owner),audio_(audio),hold_(hold),
       format_(TextFormatEnvironment::Decode(SceneRead(root/"env/env_string_format.dat"),true)),
       shared_(renderer,textures,font,resources,format_,
         TextLayoutMetrics::Decode(SceneRead(root/"env/env_global.dat"),SceneRead(root/"env/env_message_window.dat")),
         GameUtilityFont::Decode(SceneRead(root/"env/env_game_utility.dat")).Size,
         DialogEnvironment::Decode(SceneRead(root/"env/env_dialog.dat")),SceneRead(root/"first.dat")),
       storage_(saveRoot,slots,[this]{
         if(!owner_.SystemProgress)throw FormatError("system menu has no progress owner");
         return SnapshotSystemOwner(owner_,RestoreSystemReadHistory(*owner_.SystemProgress));
       }),
       save_(SaveManagerEnvironment::Decode(SceneRead(root/"env/env_save_manager.dat")),
         [this](const std::u16string& text,size_t count){return shared_.Layout(text,count);},storage_),
       menu_(SystemMenuEnvironment::Decode(SceneRead(root/"env/env_system_menu.dat")),
         ProfileTextureSection::SystemUi,hold,*this,initialSelection) {
    for(const auto& [key,texture]:renderer.Textures){(void)texture;priorTextures_.insert(key);}
  }
  ~PcSystemMenuOwner() {
    for(auto it=shared_.Renderer.Textures.begin();it!=shared_.Renderer.Textures.end();) {
      if(!priorTextures_.contains(it->first)) {
        glDeleteTextures(1,&it->second.Handle);it=shared_.Renderer.Textures.erase(it);
      }else ++it;
    }
  }
  void BeginFrame(const WidgetInputHistory::Snapshot& contacts,const DialogInput& modal,uint64_t now) {
    Now=now;ModalInput=modal;menu_.BeginFrame(contacts);
    questionContacts_.BeginFrame(contacts);saveContacts_.BeginFrame(contacts);
  }
  void CancelPointer(){menu_.CancelPointer();questionContacts_.CancelPointer();saveContacts_.CancelPointer();}
  int32_t Update(SystemMenuInput input){return menu_.Update(input,Now);}
  int32_t Phase()const{return menu_.Phase();}
  int32_t Selected()const{return menu_.Selected();}
  const DialogState& Question()const{return question_;}
  void Draw(int32_t chapterId,const std::u16string& chapterText) {
    auto& renderer=shared_.Renderer;
    glUseProgram(renderer.Program);glBindVertexArray(renderer.Vao);glBindBuffer(GL_ARRAY_BUFFER,renderer.Buffer);
    glActiveTexture(GL_TEXTURE0);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUniform1i(renderer.AlphaMode,0);renderer.SetFilter(0,0);
    for(const auto& draw:menu_.Draw(chapterId,chapterText,uint32_t(shared_.FontSize))) {
      if(const auto* c=std::get_if<UiRenderCommand>(&draw)) {
        if(c->Kind==UiRenderKind::Sprite)shared_.Sprite(c->Sprite);
        else throw FormatError("unexpected system-menu draw primitive");
      }else {
        const auto& t=std::get<UiTextBlit>(draw);
        shared_.Textures.DrawString(renderer,shared_.Font,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);
      }
    }
    shared_.Draw(question_,prompt_,layout_,questionGroup_);
    shared_.Draw(save_.Modal,save_.Prompt(),save_.Layout(),0);
    questionContacts_.Latch(question_,layout_,shared_.Environment);
    saveContacts_.Latch(save_.Modal,save_.Layout(),shared_.Environment);
  }
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  void BeginQuestion(const std::u16string& prompt,uint32_t group)override {
    if(question_.Visible || save_.State().Active)throw FormatError("system menu modal is already active");
    questionGroup_=group;prompt_=prompt;
    const auto count=shared_.Environment.ButtonGroups.at(group).size();
    layout_=shared_.Layout(prompt,count);
    BeginDialog(question_,layout_,count,0,true,true,Now);question_.NativeByte292=1;
  }
  OptionsDialogPoll PollQuestion()override {
    auto input=ModalInput;
    input.CollectActivations=[this]{return questionContacts_.Collect(question_,shared_.Environment,hold_,Now);};
    UpdateDialog(question_,input,Now);
    for(auto id:question_.UiSounds)UiSound(id);
    return {question_.Visible,question_.Canceled,question_.Result};
  }
  void BeginSystemSave()override{save_.Begin(2,Now);}
  OptionsSavePoll PollSystemSave()override {
    save_.Input=ModalInput;
    save_.Input.CollectActivations=[this]{return saveContacts_.Collect(save_.Modal,shared_.Environment,hold_,Now);};
    const auto result=save_.Update(Now);
    for(auto id:save_.Modal.UiSounds)UiSound(id);
    return {result.Active,uint8_t(result.Accepted)};
  }
  void RetireQuestion()override{question_={};}
 private:
  SemanticState& owner_;PlaybackTitleAudio& audio_;uint32_t hold_;
  TextFormatEnvironment format_;SceneDialogLayer shared_;SessionSaveWrites storage_;
  SaveManagerDialogHost save_;SystemMenu menu_;
  DialogState question_;DialogContentLayout layout_;std::u16string prompt_;size_t questionGroup_=1;
  PcDialogContacts questionContacts_,saveContacts_;std::set<ProfileTextureKey> priorTextures_;
};
