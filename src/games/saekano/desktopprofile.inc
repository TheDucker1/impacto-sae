// Private PC presenter. ProfileMode remains owned by the ongoing VirtualMachine.
class SceneProfilePresenter {
 public:
  SceneProfilePresenter(const std::filesystem::path& root, GameResources& resources,
      UiBlitRenderer& renderer, FontTextures& textures, PcFontFace& font,
      ArchiveProfileServices& services, PlaybackProfileVoice& voice,MusicPlayback& playback)
      : settings(ProfileEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::ProfileEnvironment))),
        shared(ModeResultEnvironment::DecodeApproach(ReadGameFile(root, Profile::Saekano::GameAsset::ApproachEnvironment))),
        resources_(resources),renderer_(renderer),textures_(textures),font_(font),services_(services),voice_(voice),playback_(playback),
        backdrop_(ProfileBackdropEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::ProfileEnvironment))),
        catalog_(LoadProfileItemCatalog(ReadGameFile(root, Profile::Saekano::GameAsset::FirstArchive))),
        description_(ProfileDescriptionFormat::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
        dialogEnvironment_(DialogEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::DialogEnvironment))),
        format_(TextFormatEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::StringFormatEnvironment),true)),
        metrics_(TextLayoutMetrics::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment),ReadGameFile(root, Profile::Saekano::GameAsset::MessageEnvironment))),
        utility_(GameUtilityFont::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::UtilityEnvironment))),
        resultVisual_(ModeResultVisualEnvironment::DecodeApproach(ReadGameFile(root, Profile::Saekano::GameAsset::ApproachEnvironment))),
        responseVisual_(ProfileResponseVisualEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::ProfileEnvironment))),
        cursorEnvironment_(ProfileCursorEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::ProfileEnvironment))),
        messageCursor_(MessageCursorEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::MessageEnvironment))),
        sharedUi_(ReadGameFile(root, Profile::Saekano::GameAsset::FirstArchive)) {
    VoiceTextReveal prompt;
    prompt.PrepareUtf16(DecodeProfileConfirmationPrompt(ReadGameFile(root, Profile::Saekano::GameAsset::ProfileEnvironment)),format_,0);
    promptLayout_=LayoutPlainText(prompt.Text,format_,font_.Cached(),metrics_,dialogEnvironment_.Settings[6],
        dialogEnvironment_.TextScales[1],uint32_t(utility_.Size),font_.NominalSize());
    if(dialogEnvironment_.ButtonGroups.size()<=1)throw FormatError("missing profile confirmation buttons");
    dialogLayout_=BuildDialogContentLayout(dialogEnvironment_,{480,272},promptLayout_.Height,
        dialogEnvironment_.ButtonGroups[1].size(),utility_.Size);
    hold_=WidgetInputEnvironment::DecodeUtility(ReadGameFile(root, Profile::Saekano::GameAsset::UtilityEnvironment)).HoldFrames;
    repeats_=AudioEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::UtilityEnvironment)).OpaqueTable;
    ValidateOptionsRepeatIntervals(repeats_);
    const auto rectangle=[&](std::array<int32_t,4> area){UiSpriteDefinition shape;
      shape.X=area[0];shape.Y=area[1];shape.Width=area[2];shape.Height=area[3];return shape;};
    const auto add=[&](UiSpriteDefinition shape){widgets_.emplace_back(shape,ProfileTextureKey{},1.0f,true,hold_);};
    add(rectangle(settings.Portrait.Area));
    for(const auto& shape:settings.Navigation.Sprites)add(shape);
    for(const auto& area:settings.EventButtons)add(rectangle(area));
    auto selected=settings.ItemFrames.Sprite;
    selected.X=settings.ItemFrames.Positions.at(3)[0];selected.Y=settings.ItemFrames.Positions.at(3)[1];add(selected);
    add(rectangle({0,0,960,544}));
  }
  const ProfileEnvironment settings;
  const ModeResultEnvironment shared;
  void Begin() {
    dialog_={};dialogBound_=false;cursor_={};resultLaidOut_=responseLaidOut_=false;
    CancelPointer();
    // The shared renderer retains these immutable assets between mode entries.
    for(const auto& texture:services_.MenuTextures())
      if(!renderer_.Textures.contains(texture.Key))renderer_.Upload(texture.Key,texture.Pixels);
  }
  void Handle(const SDL_Event& event,ProfileSelectionInput& input) {
    if(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) {
      for(size_t i=0;i<2;++i)if(event.key.key==(i==0?SDLK_UP:SDLK_DOWN)) {
        keyHeld_[i]=event.type==SDL_EVENT_KEY_DOWN;
        pressed_[i]|=keyHeld_[i] && !event.key.repeat;
      }
    }
    if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) {
      for(size_t i=0;i<2;++i)if(event.gbutton.button==(i==0?SDL_GAMEPAD_BUTTON_DPAD_UP:SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        padHeld_[i]=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;pressed_[i]|=padHeld_[i];
      }
    }
    if(event.type==SDL_EVENT_GAMEPAD_REMOVED)padHeld_={};
    const PcActionInput action(event);
    if(dialog_.Visible) {
      action.Apply(modal_);
    } else {
      if(action.Left)input.CharacterDirection=false;
      if(action.Right)input.CharacterDirection=true;
      input.Accept|=action.Accept;input.Cancel|=action.Cancel;
    }
  }
  void CancelPointer() {
    pointer_={};dialogContacts_.CancelPointer();
    keyHeld_=padHeld_=pressed_={};repeatCounters_={};
    for(auto& widget:widgets_){widget.Input=WidgetEventHistory(hold_);}
  }
  void BeforeStep(const ProfileMode& mode,ProfileSelectionInput& input,
      const WidgetInputHistory::Snapshot& snapshot,uint64_t now) {
    pointer_.BeginFrame(snapshot);dialogContacts_.BeginFrame(snapshot);
    if(dialog_.Visible) {
      modal_.CollectActivations=[&]{return dialogContacts_.Collect(dialog_,dialogEnvironment_,hold_,now);};
      UpdateDialog(dialog_,modal_,now);
      for(const auto sound:dialog_.UiSounds)playback_.Submit({GameEventKind::Sound,sound});
      if(!dialog_.Visible)input.Confirmation=dialog_.Result==1;
    } else {
      const auto update=[&](size_t index,std::array<int32_t,2> offset=std::array<int32_t,2>{}) {
        auto& widget=widgets_.at(index);
        widget.Update(offset,SampleWidgetContact(widget.HitRectangle(offset),pointer_.Current[0],
            pointer_.Previous[0],widget.Input.TrackedId,pointer_.Velocity(0)),now);
        pointer_.Publish(widget.Input.Flags);return widget.Input.Flags;
      };
      if(mode.CurrentStage()==ProfileMode::Stage::Selection) {
        const auto phase=mode.Selection().PendingPhase.value_or(mode.Selection().Phase);
        if(phase==5 || phase==6) {
          const auto portrait=update(0);
          if(phase==5) {
            const auto left=update(1,{mode.Navigation().Offsets[0],0});
            const auto right=update(2,{mode.Navigation().Offsets[1],0});
            if(portrait&uint16_t(WidgetQuery::Flick))input.CharacterDirection=widgets_[0].Input.Velocity[0]>0;
            else if(!input.CharacterDirection && (left&8))input.CharacterDirection=false;
            else if(!input.CharacterDirection && (right&8))input.CharacterDirection=true;
            else input.Accept|=(portrait&8)!=0;
          } else {
            // Native8018ed28: both vertical counters advance independently,
            // only on phase6 updates. Profile sets continuous/wrap; phase7
            // carousel animation suspends this owner, rather than its clock
            // using SDL's platform-dependent key-repeat events.
            auto& counters=repeatCounters_.at(mode.Selection().Ordinal);
            std::array<bool,2> active{};
            for(size_t i=0;i<2;++i) {
              if(pressed_[i]){counters[i]=0;active[i]=true;}
              else if(!keyHeld_[i] && !padHeld_[i])counters[i]=0;
              else {counters[i]=std::bit_cast<int32_t>(uint32_t(counters[i])+1);
                active[i]=OptionsRepeatDeadline(counters[i],repeats_);}
            }
            if(active[0])input.EventDirection=true;
            else if(active[1])input.EventDirection=false;
            const auto previous=update(3),next=update(4),selected=update(5);
            if(!input.EventDirection && (previous&8))input.EventDirection=true;
            else if(!input.EventDirection && (next&8))input.EventDirection=false;
            input.Accept|=(selected&8)!=0;input.Cancel|=(portrait&8)!=0;
          }
        }
      } else if((mode.CurrentStage()==ProfileMode::Stage::Result && mode.Phase()==12) ||
                (mode.CurrentStage()==ProfileMode::Stage::Response && mode.Phase()==17)) {
        input.Accept|=(update(6)&8)!=0;
      }
    }
    modal_={};
    pressed_={};
  }
  void AfterStep(const ProfileMode& mode,uint64_t now) {
    if(mode.Selection().ConfirmationOpen && !dialogBound_) {
      BeginDialog(dialog_,dialogLayout_,dialogEnvironment_.ButtonGroups[1].size(),1,false,true,now);
      dialogBound_=true;UpdateDialog(dialog_,{},now);
      for(const auto sound:dialog_.UiSounds)playback_.Submit({GameEventKind::Sound,sound});
    }
    if(!mode.Selection().ConfirmationOpen)dialogBound_=false;
    const bool tick=mode.CurrentStage()==ProfileMode::Stage::Result?
        mode.Result().CursorTickRequested:mode.Response().CursorTickRequested;
    cursor_.Update(cursorEnvironment_,tick,now);
  }
  SDL_Keycode CaptureKey(const ProfileMode& mode,std::array<size_t,2> target,uint64_t now) const {
    const auto& selection=mode.Selection();
    if(dialog_.Visible)return dialog_.Phase==3?SDLK_RETURN:0;
    if(mode.CurrentStage()==ProfileMode::Stage::Selection && !selection.PendingPhase) {
      if(selection.Phase==5)return selection.Ordinal==target[0]?SDLK_RETURN:SDLK_RIGHT;
      if(selection.Phase==6) {
        if(selection.Ordinal!=target[0])throw FormatError("profile ordinal differs from capture plan");
        return selection.Selectors.at(selection.Ordinal)==target[1]?SDLK_RETURN:SDLK_UP;
      }
    }
    if(mode.CurrentStage()==ProfileMode::Stage::Result && mode.Phase()==12)
      return services_.ResultText().Revealed && now%32==0?SDLK_RETURN:0;
    if(mode.CurrentStage()==ProfileMode::Stage::Response && mode.Phase()==17)
      return services_.ResponseText().Revealed && voice_.Stopped()?SDLK_RETURN:0;
    return 0;
  }
  std::string Signature(const ProfileMode& mode) const {
    return std::to_string(mode.Phase())+':'+std::to_string(dialog_.Phase)+':'+
        std::to_string(dialog_.Opacity.Current/50)+':'+std::to_string(services_.ResultText().Revealed)+':'+
        std::to_string(services_.ResponseText().Revealed)+':'+std::to_string(voice_.Stopped())+':'+
        std::to_string(services_.Blink().Phase)+':'+std::to_string(mode.Selection().Ordinal)+':'+
        std::to_string(mode.Selection().Selectors.at(mode.Selection().Ordinal));
  }
  void Draw(const ProfileMode& mode,const SemanticState& state,int width,int height) {
    sprites_.clear();commands_.clear();text_.clear();
    // Original8004ff38 skips every profile draw while outer phase2 loads resources.
    if(mode.CurrentStage()==ProfileMode::Stage::Loading){renderer_.Draw({},width,height);return;}
    const auto& selection=mode.Selection();const auto& scene=services_.Scene();
    const auto ordinal=selection.Ordinal;const auto phase=mode.Phase();
    const bool response=mode.Response().Visible;
    auto base=response?std::vector<UiSpriteBlit>{}:BuildProfileBackdrop(backdrop_,mode.Wave().Current);
    if(!response) {
      const auto chrome=BuildProfileMenuChrome(settings.Chrome,phase,mode.Result().SecondaryProgress.Current);
      base.insert(base.end(),chrome.begin(),chrome.end());
    }
    for(const auto& b:base)Ensure(b.Texture);
    auto frames=response?std::vector<UiRenderCommand>{}:BuildProfileItemFramePass(settings.ItemFrames,phase,960,544,selection.FrameOffsets);
    for(const auto& c:frames)if(c.Kind==UiRenderKind::Sprite)Ensure(c.Sprite.Texture);
    renderer_.Draw(base,width,height,frames);sprites_=base;commands_=frames;
    if(!response) {
      std::array<std::array<int32_t,2>,8> offsets;
      for(size_t i=0;i<8;++i)offsets[i]={selection.CarouselX[i].Current,selection.CarouselY[i].Current};
      const auto contents=BuildProfileItemContents(settings,phase,scene.at(ordinal),selection.Selectors.at(ordinal),
          catalog_.at(ordinal).Rows,utility_.Size,offsets);
      Sprite(contents.Selector);
      for(const auto& slot:contents.Slots){Sprite(slot.Icon);Text(slot.Label);}
      ResetTests();
      for(const auto& b:BuildProfileArrows(settings.Arrows,selection,mode.Arrows()))Sprite(b);
      const auto desc=BuildProfileDescription(settings,phase,scene.at(ordinal),selection.Selectors.at(ordinal),
          catalog_.at(ordinal).Rows,utility_.Size,description_.Separator);
      for(const auto& t:desc.Lines)Text(t);
      std::array<int32_t,3> changes{};
      if(mode.CurrentStage()==ProfileMode::Stage::Result && mode.Result().Stats.Started)
        for(size_t i=0;i<3;++i)changes[i]=mode.Result().Stats.Changes[i].Current;
      const auto hud=BuildProfileHud(settings.Hud,scene.at(ordinal).Id,state.ProfileOwner.Stats,changes);
      Sprite(hud.BeforePortrait);
      const auto portrait=BuildProfilePortrait(settings.Portrait,scene,ordinal);
      Sprite(portrait[0].Sprite);Solid(portrait[1].Rectangle,portrait[1].Color);
      for(const auto& b:hud.AfterPortrait)Sprite(b);
      for(const auto& b:BuildProfileNavigation(settings.Navigation,phase,mode.Navigation()))Sprite(b);
      if(dialog_.Visible) {
        const auto progress=dialog_.Opacity.Current;
        const auto frame=BuildDialogFrame(dialogEnvironment_,{480,272},dialogLayout_.Width,dialogLayout_.BodyHeight,progress,{0,0,0,90});
        Solid(frame.Overlay.Rectangle,frame.Overlay.Color);
        for(const auto& b:frame.Tiles)Sprite(b);
        for(const auto& b:BuildDialogDecoration(dialogEnvironment_,frame,progress))Sprite(b);
        for(const auto& t:BuildPlainTextDraws(promptLayout_,dialogLayout_.PromptPosition,uint32_t(utility_.Size),
            dialogEnvironment_.Colors[0],uint8_t(progress*255/100)))Text(t);
        for(const auto& button:BuildDialogButtons(dialogEnvironment_,{480,272},dialogLayout_,dialogEnvironment_.ButtonGroups[1],
            dialog_.Selected,dialog_.FocusEnabled,progress,utility_.Size,font_.Cached(),font_.NominalSize())) {
          for(const auto& b:button.Background)Sprite(b);Text(button.Label);
        }
        dialogContacts_.Latch(dialog_,dialogLayout_,dialogEnvironment_);
      }
    }
    if(response) {
      const auto& r=mode.Response();
      if(const auto* bg=services_.BackgroundPixels()) {
        const ProfileTextureKey key{ProfileTextureSection::ResponseBackground,r.Background,0};
        if(!renderer_.Textures.contains(key))renderer_.Upload(key,*bg);
        Sprite({key,{0,0,960,544},{0,0,960,544},{255,255,255,255}});
      }
      const auto* actor=services_.ActorPixels();if(!actor)throw FormatError("profile response pose missing");
      for(size_t i=0;i<actor->Textures.size();++i) {
        const ProfileTextureKey key{ProfileTextureSection::CharacterPose,actor->Pose,uint32_t(i)};
        if(!renderer_.Textures.contains(key))renderer_.Upload(key,actor->Textures[i]);
      }
      CurrentCharacterPoseDraw actorState;actorState.Face=r.Event.Presentation[1];actorState.EyePhase=services_.Blink().Phase;
      actorState.Position={700,0};actorState.MouthFrame=services_.Mouth().Frame;
      for(const auto& b:BuildCurrentCharacterPose(*actor,actorState))Sprite(b);
      const auto ui=BuildProfileResponseUi(responseVisual_,services_.ResponseText().Revealed,cursor_.Channel.Current);
      Sprite(ui.Window);
      Revealing(services_.ResponseText(),responseLayout_,responseLaidOut_,responseVisual_.TextWidth,responseVisual_.TextScale,responseVisual_.TextPosition);
      for(const auto& b:ui.Cursors)Sprite(b);
    } else if(mode.Result().Visible) {
      const auto layers=BuildProfileResultLayers(resultVisual_,settings.ResultReflection,mode.Result(),960,544);
      for(const auto& c:layers)if(c.Kind==UiRenderKind::Sprite)Ensure(c.Sprite.Texture);
      renderer_.Execute(layers);commands_.insert(commands_.end(),layers.begin(),layers.end());ResetTests();
      Revealing(services_.ResultText(),resultLayout_,resultLaidOut_,resultVisual_.TextWidth,resultVisual_.TextScale,resultVisual_.TextPosition);
      for(const auto& b:BuildProfileResultOverlays(resultVisual_,messageCursor_,mode.Result(),services_.ResultText().Revealed,cursor_.Channel.Current))Sprite(b);
    }
    const auto fade=mode.CurrentStage()==ProfileMode::Stage::Selection?selection.Fade.Current:
        mode.CurrentStage()==ProfileMode::Stage::Result?mode.Result().Fade.Current:mode.Response().Fade.Current;
    if(fade)Solid({0,0,960,544},{255,255,255,uint8_t(fade)});
    GlCheck("ongoing scene profile composition");
  }
  void Write(std::ostream& out,const ProfileMode& mode,const SemanticState& state,uint64_t now,const std::string& file) const {
    out << "{\"file\":\"" << file << "\",\"now\":" << now << ",\"mode_id\":" << state.OtakuMode
        << ",\"stage\":" << int(mode.CurrentStage()) << ",\"wave\":" << mode.Wave().Current
        << ",\"cursor_bits\":" << std::bit_cast<uint32_t>(cursor_.Channel.Current)
        << ",\"eye_phase\":" << services_.Blink().Phase << ",\"mouth_frame\":" << services_.Mouth().Frame
        << ",\"response_visible\":" << mode.Response().Visible << ",\"result_visible\":" << mode.Result().Visible
        << ",\"pose\":" << mode.Response().Event.Presentation[0] << ",\"face\":" << mode.Response().Event.Presentation[1]
        << ",\"character\":" << mode.Result().CharacterId << ",\"category\":" << mode.Result().Event.Values[1]
        << ",\"phase\":" << mode.Phase() << ",\"ordinal\":" << mode.Selection().Ordinal
        << ",\"selector\":" << mode.Selection().Selectors.at(mode.Selection().Ordinal)
        << ",\"result_revealed\":" << services_.ResultText().Revealed << ",\"response_revealed\":" << services_.ResponseText().Revealed
        << ",\"voice_stopped\":" << voice_.Stopped() << ",\"dialog_phase\":" << dialog_.Phase << ",\"owner_stats\":[";
    for(size_t i=0;i<12;++i){if(i)out << ',';out << state.ProfileOwner.Stats[i];}
    out << "],\"result_samples\":[";
    const auto& r=mode.Result();const std::array<int32_t,6> samples{r.Primary.Current,r.HorizontalOffset.Current,r.HorizontalAlpha.Current,
      r.VerticalOffset.Current,r.VerticalAlpha.Current,r.EntryCompletion.Current};
    for(size_t i=0;i<samples.size();++i){if(i)out << ',';out << samples[i];}
    out << "],\"sprites\":[";for(size_t i=0;i<sprites_.size();++i){if(i)out << ',';WriteUiBlit(out,sprites_[i]);}
    out << "],\"commands\":[";for(size_t i=0;i<commands_.size();++i){if(i)out << ',';WriteUiCommand(out,commands_[i]);}
    out << "],\"text\":[";for(size_t i=0;i<text_.size();++i){if(i)out << ',';WriteDescription(out,-1,text_[i]);}out << "]}\n";
  }
 private:
  void Ensure(ProfileTextureKey key) {
    if(renderer_.Textures.contains(key))return;
    const auto raw=key.Section==ProfileTextureSection::SharedUi?sharedUi_.Load(key.Entry).Data:resources_.LoadProfileTexture(key).Data;
    renderer_.Upload(key,DecodeSceneImage(raw));
  }
  void Sprite(const UiSpriteBlit& b){Ensure(b.Texture);renderer_.DrawQuad(b,renderer_.Textures.at(b.Texture),false);sprites_.push_back(b);}
  void Solid(std::array<int32_t,4> area,std::array<uint8_t,4> color) {
    UiSpriteBlit b;b.Destination=area;b.Color=color;renderer_.DrawQuad(b,{0,1,1},true);
    UiRenderCommand c;c.Kind=UiRenderKind::Rectangle;c.Rectangle=area;c.Color=color;commands_.push_back(c);
  }
  void Text(const UiTextBlit& t){textures_.DrawString(renderer_,font_,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);text_.push_back(t);}
  void ResetTests(){renderer_.SetAlpha(0);ViewerWindow::Gpu->SetDepthTest(5,false);ViewerWindow::Gpu->SetStencil(0);}
  void Revealing(const VoiceTextReveal& text,PlainTextLayout& layout,bool& laidOut,int32_t width,float scale,std::array<int32_t,2> origin) {
    if(!text.Prepared)return;
    if(!laidOut){layout=LayoutPlainText(text.Text,format_,font_.Cached(),metrics_,width,scale,uint32_t(utility_.Size),font_.NominalSize());laidOut=true;}
    for(const auto& t:BuildRevealingTextDraws(layout,text,format_,origin,uint32_t(utility_.Size),{0,0,0,255},255))Text(t);
  }
  GameResources& resources_;UiBlitRenderer& renderer_;FontTextures& textures_;PcFontFace& font_;
  ArchiveProfileServices& services_;PlaybackProfileVoice& voice_;MusicPlayback& playback_;
  ProfileBackdropEnvironment backdrop_;std::vector<ProfileItemCharacterTable> catalog_;
  ProfileDescriptionFormat description_;DialogEnvironment dialogEnvironment_;TextFormatEnvironment format_;
  TextLayoutMetrics metrics_;GameUtilityFont utility_;ModeResultVisualEnvironment resultVisual_;
  ProfileResponseVisualEnvironment responseVisual_;ProfileCursorEnvironment cursorEnvironment_;
  MessageCursorEnvironment messageCursor_;SharedUiAssets sharedUi_;
  PlainTextLayout promptLayout_,resultLayout_,responseLayout_;DialogContentLayout dialogLayout_;
  DialogState dialog_;DialogInput modal_;ProfileCursorMotion cursor_;
  uint32_t hold_=0;WidgetInputHistory pointer_;PcDialogContacts dialogContacts_;
  std::vector<int32_t> repeats_;
  std::array<std::array<int32_t,2>,4> repeatCounters_{};
  std::array<bool,2> keyHeld_{},padHeld_{},pressed_{};
  std::vector<BattleCardSprite> widgets_;
  bool dialogBound_=false,resultLaidOut_=false,responseLaidOut_=false;
  std::vector<UiSpriteBlit> sprites_;std::vector<UiRenderCommand> commands_;std::vector<UiTextBlit> text_;
};
