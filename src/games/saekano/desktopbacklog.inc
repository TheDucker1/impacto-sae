// Existing text, widget, viewport, dialog and physical voice services consume
// the Vita backlog screen. History records remain owned by the live session.
class PcBacklogOwner final:public BacklogScreenServices {
 public:
  uint64_t Now=0;size_t ReplayStarts=0,DrawnRows=0;
  BacklogInput Input;DialogInput ModalInput;
  PcBacklogOwner(SemanticState& owner,GameResources& resources,UiBlitRenderer& renderer,
      FontTextures& textures,PcFontFace& font,PlaybackTitleAudio& audio,SceneMessageVoiceOwner& voices,
      const std::filesystem::path& root,std::span<const int32_t> repeats,uint32_t hold)
      :owner_(owner),renderer_(renderer),audio_(audio),voices_(voices),hold_(hold),
       format_(TextFormatEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::StringFormatEnvironment),true)),
       shared_(renderer,textures,font,resources,format_,
          TextLayoutMetrics::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment),ReadGameFile(root, Profile::Saekano::GameAsset::MessageEnvironment)),
          GameUtilityFont::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::UtilityEnvironment)).Size,
          DialogEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::DialogEnvironment)),ReadGameFile(root, Profile::Saekano::GameAsset::FirstArchive)),
       ordinary_(OrdinaryTextEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::MessageEnvironment),true)),
       screen_(BacklogEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::BacklogEnvironment)),owner.Backlog.Count(),repeats,hold,*this),
       help_(ButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment))),
       helpStyle_(ButtonHelpStyle::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))){
    for(const auto& [key,texture]:renderer.Textures){(void)texture;priorTextures_.insert(key);}
    for(size_t row=0;row<owner.Backlog.Count();++row){
      std::vector<Text> text;
      for(const auto& retained:owner.Backlog.At(row).Text){
        auto parsed=ParseOrdinaryMessageControls(retained.Source,format_);
        VoiceTextReveal normalized;normalized.PrepareUtf16(parsed.Body,format_,0);
        normalized.Reveal(0); // Backlog is static retained text, not a live VM reveal.
        auto layout=LayoutPlainText(normalized.Text,format_,font.Cached(),shared_.Metrics,
          int32_t(retained.LayoutParameter),retained.Scale,uint32_t(shared_.FontSize),font.NominalSize(),parsed.Ruby);
        text.push_back({std::move(parsed),std::move(normalized),std::move(layout),retained.Primary});
      }
      text_.push_back(std::move(text));
    }
  }
  ~PcBacklogOwner(){
    for(auto it=renderer_.Textures.begin();it!=renderer_.Textures.end();){
      if(!priorTextures_.contains(it->first)){Impacto::Renderer->FreeTexture(it->second.Handle);it=renderer_.Textures.erase(it);}else ++it;
    }
  }
  int32_t Phase()const{return screen_.Phase();}
  int32_t Selected()const{return screen_.Selected();}
  int32_t FirstVisible()const{return screen_.List().Rows.Scroll;}
  bool Scrolling()const{return screen_.List().Scrolling;}
  const DialogState& Question()const{return question_;}
  void BeginFrame(const BacklogInput& input,const DialogInput& modal,const WidgetInputHistory::Snapshot& contacts,uint64_t now){
    Now=now;Input=input;ModalInput=modal;screen_.BeginFrame(contacts);questionContacts_.BeginFrame(contacts);
  }
  void CancelPointer(){screen_.CancelPointer();questionContacts_.CancelPointer();}
  int32_t Update(uint64_t now){
    Now=now;
    if(activeRow_>=0 && !voices_.Output.Playing(7) && replayNext_<replayIds_.size())StartNextVoice();
    return screen_.Update(Input,now);
  }
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  bool VoicePlaying(size_t row)const override{return activeRow_==int32_t(row) && voices_.Output.Playing(7);}
  void ToggleVoice(size_t row)override{
    if(VoicePlaying(row)){StopSceneDialogueVoice(voices_,7);activeRow_=-1;replayIds_.clear();return;}
    std::vector<int16_t> ids;
    for(const auto& text:owner_.Backlog.At(row).Text)if(text.VoiceId>=0){
      if(text.VoiceId>INT16_MAX)throw FormatError("backlog voice exceeds native ID range");
      ids.push_back(int16_t(text.VoiceId));
    }
    if(ids.empty())return;
    StopSceneDialogueVoice(voices_,7);activeRow_=int32_t(row);replayIds_=std::move(ids);replayNext_=0;StartNextVoice();
  }
  void BeginRewindQuestion(const std::u16string& prompt)override{
    prompt_=prompt;questionLayout_=shared_.Layout(prompt_,shared_.Environment.ButtonGroups.at(1).size());
    BeginDialog(question_,questionLayout_,shared_.Environment.ButtonGroups.at(1).size(),1,false,true,Now);
  }
  OptionsDialogPoll PollRewindQuestion()override{
    auto input=ModalInput;input.CollectActivations=[this]{return questionContacts_.Collect(question_,shared_.Environment,hold_,Now);};
    UpdateDialog(question_,input,Now);for(auto id:question_.UiSounds)UiSound(id);
    return {question_.Visible,question_.Canceled,question_.Result};
  }
  void RetireRewindQuestion()override{question_={};}
  void Draw(){
    const auto& e=screen_.Environment();
    renderer_.Bind();
    ViewerWindow::Gpu->SetBlend({});
    ViewerWindow::Gpu->SetBlend({true,4,5,4,5});renderer_.SetAlpha(0);renderer_.SetFilter(0,0);
    auto shade=e.Colors.at(0xc1a4);shade[3]=uint8_t(int64_t(shade[3])*screen_.Fade()/e.Word(0xc19c));
    Rectangle({0,0,960,544},shade);
    Sprite(0xbe28,ProfileTextureSection::MainUi,33);Sprite(0xc11c,ProfileTextureSection::SharedUi,6);
    for(const auto& text:BuildButtonHelp(help_.CommonItems,help_.CommonRectangle,helpStyle_,shared_.Font.Cached(),
        uint32_t(shared_.FontSize),shared_.Font.NominalSize()))DrawText(text);
    for(const auto& sprite:screen_.Widgets())shared_.Sprite(sprite);
    for(const auto& command:BuildSlotViewport(screen_.Viewport())){
      if(command.Kind==SlotViewportKind::Render)renderer_.Execute(std::span<const UiRenderCommand>(&command.Render,1));
      else if(command.Kind==SlotViewportKind::Cards)DrawCards(command.Cards);
    }
    Sprite(0xbf54,ProfileTextureSection::MainUi,40,0,e.Points.at(0xbf78));
    Sprite(0xbf30,ProfileTextureSection::MainUi,40);
    Sprite(0xc018,ProfileTextureSection::SharedUi,10);
    Sprite(0xc03c,ProfileTextureSection::SharedUi,10,0,e.Points.at(0xc060));
    Sprite(0xc068,ProfileTextureSection::SharedUi,10);
    if(owner_.Backlog.Count()){
      const auto track=e.Points.at(0xc060)[1];
      float height=float(track)/float(owner_.Backlog.Count());int32_t position=0;
      if(height<12){height=12;position=owner_.Backlog.Count()>1?int32_t(int64_t(track-12)*Selected()/int32_t(owner_.Backlog.Count()-1)):0;}
      else position=int32_t(height*Selected());
      const auto y=e.Sprites.at(0xc03c).Y+position;
      const auto middle=int32_t(height)-e.Sprites.at(0xc08c).Height-e.Sprites.at(0xc0d4).Height;
      Sprite(0xc08c,ProfileTextureSection::SharedUi,11,y,std::array{e.Points.at(0xc0f8)[0],e.Sprites.at(0xc08c).Height});
      Sprite(0xc0b0,ProfileTextureSection::SharedUi,11,y,std::array{e.Points.at(0xc0f8)[0],middle});
      Sprite(0xc0d4,ProfileTextureSection::SharedUi,11,y+e.Sprites.at(0xc0b0).Y+middle,
          std::array{e.Points.at(0xc0f8)[0],e.Sprites.at(0xc0d4).Height});
    }
    shared_.Draw(question_,prompt_,questionLayout_,1,{255,255,255,90});
    questionContacts_.Latch(question_,questionLayout_,shared_.Environment);
    Rectangle({0,0,960,544},{255,255,255,uint8_t(screen_.RewindFade())});
    GlCheck("backlog frame composition");
  }
 private:
  struct Text{OrdinaryMessageText Parsed;VoiceTextReveal Reveal;PlainTextLayout Body;std::array<uint8_t,4> Color;};
  SemanticState& owner_;UiBlitRenderer& renderer_;PlaybackTitleAudio& audio_;SceneMessageVoiceOwner& voices_;uint32_t hold_;
  TextFormatEnvironment format_;SceneDialogLayer shared_;OrdinaryTextEnvironment ordinary_;BacklogScreen screen_;
  ButtonHelpEnvironment help_;ButtonHelpStyle helpStyle_;
  DialogState question_;DialogContentLayout questionLayout_;std::u16string prompt_;PcDialogContacts questionContacts_;
  std::vector<std::vector<Text>> text_;std::set<ProfileTextureKey> priorTextures_;
  int32_t activeRow_=-1;std::vector<int16_t> replayIds_;size_t replayNext_=0;
  void StartNextVoice(){voices_.Output.Prepare(7,replayIds_.at(replayNext_++));voices_.Output.Start(7);++ReplayStarts;}
  void Rectangle(std::array<int32_t,4> r,std::array<uint8_t,4> color){UiRenderCommand c;c.Kind=UiRenderKind::Rectangle;c.Rectangle=r;c.Color=color;renderer_.Execute(std::span<const UiRenderCommand>(&c,1));}
  void DrawText(const UiTextBlit& t){shared_.Textures.DrawString(renderer_,shared_.Font,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);}
  void Sprite(uint16_t at,ProfileTextureSection bank,uint32_t id,int32_t y=0,std::optional<std::array<int32_t,2>> size={}){
    auto s=screen_.Environment().Sprites.at(at);const auto source=std::array{s.Width,s.Height};s.Y+=y;
    if(size){s.Width=(*size)[0];s.Height=(*size)[1];}
    auto draw=ResolveUiSprite({bank,-1,id},s);draw.Source[2]=source[0];draw.Source[3]=source[1];shared_.Sprite(draw);
  }
  void DrawCards(const SlotCardRange& range){
    const auto& e=screen_.Environment();const auto pitch=e.Points.at(0xc100)[1];
    const auto selectedY=e.Points.at(0xbef4)[1]+e.Points.at(0xbf04)[1]+pitch*(range.Selected-range.FirstVisible)+range.Offsets[1];
    Sprite(0xbe80,ProfileTextureSection::MainUi,34,selectedY);
    const auto frameY=selectedY+e.Sprites.at(0xbe80).Y;
    Sprite(0xbea4,ProfileTextureSection::MainUi,34,frameY,std::array{e.Sprites.at(0xbea4).Width,e.Points.at(0xbec8)[1]-e.Sprites.at(0xbed0).Height});
    Sprite(0xbed0,ProfileTextureSection::MainUi,34,frameY+e.Sprites.at(0xbea4).Y+e.Points.at(0xbec8)[1]-e.Sprites.at(0xbed0).Height);
    for(auto row=range.Begin;row<range.End;++row){
      if(row<0)continue;
      const auto y=e.Points.at(0xbef4)[1]+pitch*(row-range.FirstVisible)+range.Offsets[0];
      Sprite(0xbe5c,ProfileTextureSection::MainUi,35,y);
      if(size_t(row)>=text_.size())continue;++DrawnRows;
      const auto rootY=y+e.Sprites.at(0xbe5c).Y+e.Sprites.at(0xbe5c).Height;
      const auto x=e.Points.at(0xbef4)[0]+e.Points.at(0xbf04)[0];
      const auto alpha=uint32_t(255*screen_.Fade()/e.Word(0xc19c));
      for(const auto& text:text_.at(size_t(row))){
        // Native581e->19ea0->18750 uses the ordinary record renderer with
        // Backlog's supplied name/body origins and retained style/scale.
        auto style=ordinary_;style.BodyScale=style.NameScale=text.Body.Scale;style.NameColor=text.Color;
        style.NamePosition={x,rootY+e.Points.at(0xbefc)[1]};
        style.Window.TextPosition={x,rootY+e.Points.at(0xbf04)[1]};
        for(auto draw:BuildOrdinaryMessageTextDraws(style,text.Parsed,text.Reveal,text.Body,format_,shared_.Font.Cached(),
            uint32_t(shared_.FontSize),shared_.Font.NominalSize(),100,{0,0},text.Color)){
          draw.Color[3]=uint8_t(uint32_t(draw.Color[3])*alpha/255);DrawText(draw);
        }
      }
      const auto& retained=owner_.Backlog.At(size_t(row));
      if(std::any_of(retained.Text.begin(),retained.Text.end(),[](const auto& text){return text.VoiceId>=0;})){
        const auto& shape=e.VoiceIcons.at(VoicePlaying(size_t(row))?size_t(screen_.VoiceIcon()):0);
        auto icon=shape;icon.Y+=rootY;shared_.Sprite(ResolveUiSprite({ProfileTextureSection::MainUi,-1,36},icon));
      }
    }
  }
};
