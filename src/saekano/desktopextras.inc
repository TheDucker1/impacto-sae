// Physical consumer of the native Extras root presentation and shared modals.
// Child ownership stays with the screen controller; Draw requires that actual
// child's renderer whenever ExtrasRoot reports a live child.
class PcExtrasPresentation {
 public:
  PcExtrasPresentation(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,
      FontTextures& textures,PcFontFace& font,PlaybackTitleAudio& audio,
      const std::filesystem::path& root,const std::filesystem::path& saveRoot,
      const NativeSlotData& slots,uint32_t hold,std::function<void(bool)> switchAudio)
      :owner_(owner),audio_(audio),hold_(hold),switchAudio_(std::move(switchAudio)),
       format_(TextFormatEnvironment::Decode(SceneRead(root/"env/env_string_format.dat"),true)),
       shared_(renderer,textures,font,resources,format_,
         TextLayoutMetrics::Decode(SceneRead(root/"env/env_global.dat"),SceneRead(root/"env/env_message_window.dat")),
         GameUtilityFont::Decode(SceneRead(root/"env/env_game_utility.dat")).Size,
         DialogEnvironment::Decode(SceneRead(root/"env/env_dialog.dat")),SceneRead(root/"first.dat")),
       help_(ExtrasButtonHelpEnvironment::Decode(SceneRead(root/"env/env_help_mgr.dat"))),
       style_(ButtonHelpStyle::Decode(SceneRead(root/"env/env_global.dat"))),
       storage_(saveRoot,slots,[this]{
         if(!owner_.SystemProgress)throw FormatError("Extras system owner is missing");
         return SnapshotSystemOwner(owner_,RestoreSystemReadHistory(*owner_.SystemProgress));
       }),
       save_(SaveManagerEnvironment::Decode(SceneRead(root/"env/env_save_manager.dat")),
         [this](const std::u16string& prompt,size_t count){return shared_.Layout(prompt,count);},storage_) {
    if(!switchAudio_)throw FormatError("Extras audio owner is missing");
    const auto& groups=shared_.Environment.ButtonGroups;
    if(groups.size()<=1 || groups[1].empty() || groups[1].size()>3)
      throw FormatError("Extras save-question button group is unavailable");
    for(const auto& [key,texture]:renderer.Textures){(void)texture;priorTextures_.insert(key);}
    ScopedSdkGlState scope;TargetBinding binding;
    try {
      glGenTextures(1,&target_.Handle);glBindTexture(GL_TEXTURE_2D,target_.Handle);
      // Native format9 -> color format0 / texture0x0c000000; final flag1
      // requests LINEAR min/mag. The newly allocated native surface is zeroed.
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      const std::vector<uint8_t> zero(size_t(target_.Width)*target_.Height*4);
      glPixelStorei(GL_UNPACK_ALIGNMENT,4);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,target_.Width,target_.Height,0,GL_RGBA,GL_UNSIGNED_BYTE,zero.data());
      glGenFramebuffers(1,&framebuffer_);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer_);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target_.Handle,0);
      if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw FormatError("Extras render target is incomplete");
      GlCheck("Extras render target allocation");
    }catch(...){ReleaseTarget();throw;}
  }
  PcExtrasPresentation(const PcExtrasPresentation&)=delete;
  PcExtrasPresentation& operator=(const PcExtrasPresentation&)=delete;
  ~PcExtrasPresentation(){
    ReleaseTarget();
    for(auto i=shared_.Renderer.Textures.begin();i!=shared_.Renderer.Textures.end();){
      if(!priorTextures_.contains(i->first)){
        glDeleteTextures(1,&i->second.Handle);i=shared_.Renderer.Textures.erase(i);
      }else ++i;
    }
  }
  void BeginFrame(const DialogInput& input,const WidgetInputHistory::Snapshot& contacts,uint64_t now){
    input_=input;now_=now;questionContacts_.BeginFrame(contacts);saveContacts_.BeginFrame(contacts);
  }
  void CancelPointer(){questionContacts_.CancelPointer();saveContacts_.CancelPointer();}
  void UiSound(int32_t id){audio_.PlayUi(id);}
  SceneDialogLayer& Ui(){return shared_;}
  const DialogState& Question()const{return question_;}
  const SaveManagerDialogHost& Save()const{return save_;}
  void SetExtrasAudio(bool active){switchAudio_(active);}
  void BeginSaveQuestion(const std::u16string& prompt){
    if(question_.Visible || save_.State().Active)throw FormatError("Extras modal is already active");
    if(prompt.empty())throw FormatError("Extras save question is unavailable");
    prompt_=prompt;
    const auto count=shared_.Environment.ButtonGroups[1].size();layout_=shared_.Layout(prompt_,count);
    // Native08a8c(dialog,1,0,0,1): group1, no cancel, initial0, close.
    // Native byte291 is not consumed by UpdateDialog; do not invent a flag.
    BeginDialog(question_,layout_,count,0,false,true,now_);questionOwned_=true;
  }
  OptionsDialogPoll UpdateSaveQuestion(){
    if(!questionOwned_)throw FormatError("Extras question update has no owner");
    auto input=input_;
    input.CollectActivations=[this]{return questionContacts_.Collect(question_,shared_.Environment,hold_,now_);};
    UpdateDialog(question_,input,now_);for(auto id:question_.UiSounds)UiSound(id);
    return {question_.Visible,question_.Canceled,question_.Result};
  }
  void BeginSystemSave(){
    if(question_.Visible)throw FormatError("Extras save question is still visible");
    save_.Begin(2,now_);questionOwned_=false;
  }
  OptionsSavePoll UpdateSystemSave(){
    save_.Input=input_;
    save_.Input.CollectActivations=[this]{return saveContacts_.Collect(save_.Modal,shared_.Environment,hold_,now_);};
    const auto result=save_.Update(now_);for(auto id:save_.Modal.UiSounds)UiSound(id);
    return {result.Active,uint8_t(result.Accepted)};
  }
  void Draw(const ExtrasRoot& root,const std::function<void()>& drawChild){
    if(root.ChildPresent() && !drawChild)throw FormatError("Extras child renderer is missing");
    ScopedSdkGlState scope;TargetBinding binding;BindUi();
    const auto text=[this](const UiTextBlit& t){
      shared_.Textures.DrawString(shared_.Renderer,shared_.Font,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);
    };
    const auto measure=[this](std::u16string_view value,uint32_t size){
      return std::bit_cast<int32_t>(MeasurePvfText(shared_.Font.Cached(),value,size,shared_.Font.NominalSize()));
    };
    for(const auto& command:BuildExtrasRootDraw(root,uint32_t(shared_.FontSize),measure))switch(command.Kind){
      case ExtrasDrawKind::BeginMask:
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,framebuffer_);glViewport(0,0,target_.Width,target_.Height);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);ApplyParticleBlend(1);break;
      case ExtrasDrawKind::Multiply:ApplyParticleBlend(3);break;
      case ExtrasDrawKind::EndMask:binding.Restore();ApplyParticleBlend(1);break;
      case ExtrasDrawKind::Sprite:shared_.Sprite(command.Sprite);break;
      case ExtrasDrawKind::Mask:{
        // UI coordinates start at the top; the GL target's texture starts at
        // the bottom. Flip only its sampling, retaining authored destination.
        auto blit=command.Sprite;
        blit.Source[1]=std::bit_cast<int32_t>(target_.Height-uint32_t(blit.Source[1]));
        blit.Source[3]=std::bit_cast<int32_t>(0u-uint32_t(blit.Source[3]));
        shared_.Renderer.DrawQuad(blit,target_,false);break;
      }
      case ExtrasDrawKind::Child:drawChild();binding.Restore();BindUi();break;
      case ExtrasDrawKind::Help:
        for(const auto& t:BuildButtonHelp(help_.Items,help_.Rectangle,style_,shared_.Font.Cached(),
            uint32_t(shared_.FontSize),shared_.Font.NominalSize()))text(t);
        break;
      case ExtrasDrawKind::Text:text(command.Text);break;
      case ExtrasDrawKind::Fade:shared_.Renderer.DrawQuad(command.Sprite,{0,1,1},true);break;
      case ExtrasDrawKind::Overlays:
        shared_.Draw(question_,prompt_,layout_,1,{255,255,255,90});
        shared_.Draw(save_.Modal,save_.Prompt(),save_.Layout(),0);
        questionContacts_.Latch(question_,layout_,shared_.Environment);
        saveContacts_.Latch(save_.Modal,save_.Layout(),shared_.Environment);break;
    }
    GlCheck("Extras presentation");
  }
 private:
  // Compatibility attribute stacks do not preserve framebuffer bindings.
  struct TargetBinding {
    GLint Draw=0,Read=0;std::array<GLint,4> Viewport{};
    TargetBinding(){
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&Draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&Read);
      glGetIntegerv(GL_VIEWPORT,Viewport.data());
    }
    void Restore()const{
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(Draw));glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(Read));
      glViewport(Viewport[0],Viewport[1],Viewport[2],Viewport[3]);
    }
    ~TargetBinding(){Restore();}
  };
  void BindUi(){
    auto& renderer=shared_.Renderer;
    glUseProgram(renderer.Program);glBindVertexArray(renderer.Vao);glBindBuffer(GL_ARRAY_BUFFER,renderer.Buffer);
    glActiveTexture(GL_TEXTURE0);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUniform1i(renderer.AlphaMode,0);renderer.SetFilter(0,0);ApplyParticleBlend(1);
  }
  void ReleaseTarget()noexcept{
    if(framebuffer_)glDeleteFramebuffers(1,&framebuffer_);
    if(target_.Handle)glDeleteTextures(1,&target_.Handle);
    framebuffer_=target_.Handle=0;
  }
  SemanticState& owner_;PlaybackTitleAudio& audio_;uint32_t hold_;
  std::function<void(bool)> switchAudio_;TextFormatEnvironment format_;SceneDialogLayer shared_;
  ExtrasButtonHelpEnvironment help_;ButtonHelpStyle style_;
  SessionSaveWrites storage_;SaveManagerDialogHost save_;
  DialogState question_;std::u16string prompt_;DialogContentLayout layout_;
  PcDialogContacts questionContacts_,saveContacts_;DialogInput input_;uint64_t now_=0;
  bool questionOwned_=false;
  UiBlitRenderer::BoundTexture target_{0,960,544};GLuint framebuffer_=0;
  std::set<ProfileTextureKey> priorTextures_;
};
