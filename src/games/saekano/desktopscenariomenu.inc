// Function-scope scenario ownership. Existing screens/services do the work;
// this boundary arbitrates input, entry publication and manager replacement.
std::unique_ptr<PcSystemMenuOwner> systemMenu;
std::unique_ptr<PcSlotOwner> scenarioSlots;
std::unique_ptr<PcOptionsOwner> scenarioOptions;
std::unique_ptr<PcBacklogOwner> scenarioBacklog;
SystemMenuInput systemMenuInput;
SlotBrowserInput scenarioSlotInput;
OptionsPageInput scenarioOptionsInput;
BacklogInput scenarioBacklogInput;
DialogInput scenarioModalInput;
std::array<bool,2> modalVertical{},modalHorizontal{};
bool requestSystemMenu=false;
bool captureBacklogImage=false;
bool returningTitle=false;
TitleTween titleReturnFade;
DialogState replayQuestion;
DialogContentLayout replayQuestionLayout;
PcDialogContacts replayQuestionContacts;
TitleTween replayReturnFade;
bool replayInterrupt=false,returningReplay=false;
std::u16string replayQuestionPrompt;
int32_t replayFadeDuration=0;
if(replayScript){
  const auto replayEnv=ExtrasScreenEnvironment::DecodeThroughBattle(ReadGameFile(root, Profile::Saekano::GameAsset::ExtrasEnvironment));
  replayQuestionPrompt=replayEnv.Strings.at(0xde98);replayFadeDuration=replayEnv.Common.FadeDuration;
  if(replayFadeDuration<0)throw FormatError("negative replay return fade duration");
}
const auto beginReturnTitle=[&](uint64_t now,bool alreadyOpaque=false) {
  if(returningTitle)return;
  returningTitle=true;
  titleReturnFade.Configure(alreadyOpaque?255:1,255,
      alreadyOpaque?1:backend.GlobalUiSettings->FadeDuration,true,now);
};
int32_t previousMenuAction=0;
const auto scenarioRepeat=AudioEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::UtilityEnvironment)).OpaqueTable;
const auto optionByte=[&](size_t index) {
  return vm->State().SystemProgress?vm->State().SystemProgress->Bytes6022c.at(index):uint8_t(8);
};
PlaybackTitleAudio scenarioUiAudio(playback,ProfileVoiceGain(optionByte(10),1));
TitleAudioOwner scenarioMenuVoices(resources,scenarioUiAudio);
auto& scenarioAttenuation=*playback.VoiceAttenuation();
const auto scenarioAudioClock=[&]()->uint64_t{return playback.AudioServiceTime();};
scenarioUiAudio.BindVoiceAttenuation(scenarioAttenuation,scenarioAudioClock);
const SavePreviewFormatter scenarioSaveFormatter{format,font.Cached(),metrics,
    uint32_t(utility.Size),font.NominalSize(),SavePreviewSettings::FromSlotEnvironment(
      SlotEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::SaveLoadEnvironment)))};
const auto modalOwned=[&]{return bool(systemMenu || scenarioSlots || scenarioOptions || scenarioBacklog || returningTitle || replayQuestion.Visible || returningReplay);};
const auto openSystemMenu=[&] {
  if(!slotProgress || progressRoot.empty())throw UnsupportedCommand("scenario menu needs a writable session owner");
  systemMenu=std::make_unique<PcSystemMenuOwner>(vm->State(),resources,renderer,
      fontTextures,font,scenarioUiAudio,root,progressRoot,*slotProgress,
      inputEnvironment.HoldFrames,previousMenuAction>0?previousMenuAction-1:0);
};
const auto enterSystemMenu=[&] {
  // Native790ba stops physical voices, clears sprite envelope Active/Armed,
  // clears the primary prepared vector (M1618=M1614) and requests lane flush.
  for(uint32_t channel:{7u,8u})if(playback.OrdinaryVoicePlaying(channel))
    StopSceneDialogueVoice(*voiceOwner,channel);
  const auto& control=vm->State().MouthControl;
  if(control && control->OverrideEnabled) {
    for(auto id:control->OverrideIds)if(id>=0)mouths.at(uint16_t(id)).Stop();
  }else for(auto& [id,mouth]:mouths){(void)id;mouth.Stop();}
  voiceOwner->Queue.Prepared.clear();
  for(uint32_t i=0;i<2;++i)StopSceneDialogueVoice(*voiceOwner,voiceOwner->Queue.BaseChannel+i);
  vm->RequestLaneFlush();
  vm->RefreshSavePresentation(scenarioSaveFormatter,sceneThumbnail.Capture());
  if(vm->State().SystemProgress)StoreSystemReadHistory(*vm->State().SystemProgress,registeredHistory);
  previousMenuAction=0;openSystemMenu();
  sceneInput.CancelPointer();pointerDown=false;autoKeyHeld=false;skipKeysHeld={};
};
const auto handleModalEvent=[&](const SDL_Event& e) {
  if(e.type==SDL_EVENT_QUIT){quit=true;return;}
  if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) {
    const bool down=e.type==SDL_EVENT_KEY_DOWN,pressed=down&&!e.key.repeat;
    const auto key=e.key.key;
    if(key==SDLK_UP || key==SDLK_DOWN) {
      const size_t i=key==SDLK_DOWN;modalVertical[i]=down;
      scenarioSlotInput.Vertical.Pressed[i]|=pressed;
      scenarioOptionsInput.Vertical.Pressed[i]|=pressed;
      scenarioBacklogInput.Vertical.Pressed[i]|=pressed;
      if(i)systemMenuInput.Next|=pressed;else systemMenuInput.Previous|=pressed;
    }
    if(key==SDLK_LEFT || key==SDLK_RIGHT) {
      const size_t i=key==SDLK_LEFT;modalHorizontal[i]=down;
      scenarioOptionsInput.HorizontalPressed[i]|=pressed;
      scenarioModalInput.Left|=pressed&&i;scenarioModalInput.Right|=pressed&&!i;
    }
    if(pressed) {
      const bool accept=key==SDLK_RETURN,cancel=key==SDLK_ESCAPE||key==SDLK_BACKSPACE;
      systemMenuInput.Accept|=accept;systemMenuInput.Cancel|=cancel;
      scenarioSlotInput.Accept|=accept;scenarioSlotInput.Cancel|=cancel;
      scenarioSlotInput.Protect|=key==SDLK_SPACE;
      scenarioOptionsInput.Accept|=accept;scenarioOptionsInput.Preview|=key==SDLK_SPACE;
      scenarioOptionsInput.Common.Close|=key==SDLK_F13;
      scenarioOptionsInput.Common.Revert|=cancel;
      scenarioOptionsInput.Common.PreviousTab|=key==SDLK_Q||key==SDLK_PAGEUP;
      scenarioOptionsInput.Common.NextTab|=key==SDLK_E||key==SDLK_PAGEDOWN;
      scenarioModalInput.Accept|=accept;scenarioModalInput.Cancel|=cancel;
      scenarioBacklogInput.Rewind|=accept;scenarioBacklogInput.Replay|=key==SDLK_SPACE;scenarioBacklogInput.Cancel|=cancel;
    }
  }
  if(e.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    const auto b=e.gbutton.button;
    systemMenuInput.Previous|=b==SDL_GAMEPAD_BUTTON_DPAD_UP;
    systemMenuInput.Next|=b==SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    systemMenuInput.Accept|=b==SDL_GAMEPAD_BUTTON_EAST;
    systemMenuInput.Cancel|=b==SDL_GAMEPAD_BUTTON_SOUTH;
    scenarioSlotInput.Vertical.Pressed[0]|=b==SDL_GAMEPAD_BUTTON_DPAD_UP;
    scenarioSlotInput.Vertical.Pressed[1]|=b==SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    scenarioSlotInput.Accept|=b==SDL_GAMEPAD_BUTTON_EAST;scenarioSlotInput.Cancel|=b==SDL_GAMEPAD_BUTTON_SOUTH;
    scenarioOptionsInput.Vertical.Pressed[0]|=b==SDL_GAMEPAD_BUTTON_DPAD_UP;
    scenarioOptionsInput.Vertical.Pressed[1]|=b==SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    scenarioOptionsInput.Accept|=b==SDL_GAMEPAD_BUTTON_EAST;
    scenarioOptionsInput.Common.Close|=b==SDL_GAMEPAD_BUTTON_SOUTH;
    scenarioModalInput.Left|=b==SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    scenarioModalInput.Right|=b==SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    scenarioModalInput.Accept|=b==SDL_GAMEPAD_BUTTON_EAST;scenarioModalInput.Cancel|=b==SDL_GAMEPAD_BUTTON_SOUTH;
    scenarioBacklogInput.Vertical.Pressed[0]|=b==SDL_GAMEPAD_BUTTON_DPAD_UP;
    scenarioBacklogInput.Vertical.Pressed[1]|=b==SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    scenarioBacklogInput.Rewind|=b==SDL_GAMEPAD_BUTTON_EAST;scenarioBacklogInput.Replay|=b==SDL_GAMEPAD_BUTTON_WEST;
    scenarioBacklogInput.Cancel|=b==SDL_GAMEPAD_BUTTON_SOUTH;
  }
  if(e.type==SDL_EVENT_MOUSE_WHEEL && scenarioBacklog && e.wheel.y!=0)scenarioBacklogInput.Vertical.Pressed[e.wheel.y<0]=true;
  if((e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP) && e.button.button==SDL_BUTTON_LEFT) {
    pointerDown=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN;pointerPosition={e.button.x,e.button.y};
  }
  if(e.type==SDL_EVENT_MOUSE_MOTION)pointerPosition={e.motion.x,e.motion.y};
  if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
    pointerDown=false;modalVertical=modalHorizontal={};
    if(systemMenu)systemMenu->CancelPointer();if(scenarioSlots)scenarioSlots->CancelPointer();
    if(scenarioOptions)scenarioOptions->CancelPointer();
    if(scenarioBacklog)scenarioBacklog->CancelPointer();
    if(replayQuestion.Visible)replayQuestionContacts.CancelPointer();
  }
};
const auto updateScenarioModal=[&](uint64_t now) {
  WidgetInputHistory::Snapshot contacts;
  if(pointerDown) {
    int width=0,height=0;SdlCheck(SDL_GetWindowSize(window.Window,&width,&height),"modal input viewport");
    contacts[0]=WidgetTouch{0,{int16_t(pointerPosition[0]*960/width),int16_t(pointerPosition[1]*544/height)}};
  }
  scenarioSlotInput.Vertical.Held=modalVertical;scenarioOptionsInput.Vertical.Held=modalVertical;
  scenarioOptionsInput.HorizontalHeld=modalHorizontal;
  scenarioBacklogInput.Vertical.Held=modalVertical;
  if(replayQuestion.Visible){
    replayQuestionContacts.BeginFrame(contacts);
    auto input=scenarioModalInput;
    input.CollectActivations=[&]{return replayQuestionContacts.Collect(replayQuestion,endingLayer.Environment,inputEnvironment.HoldFrames,now);};
    UpdateDialog(replayQuestion,input,now);
    for(const auto id:replayQuestion.UiSounds)scenarioUiAudio.PlayUi(id);
    if(!replayQuestion.Visible){
      replayQuestionContacts.CancelPointer();sceneInput.CancelPointer();
      if(!replayQuestion.Canceled && replayQuestion.Result==1){
        returningReplay=true;replayReturnFade.Configure(0,255,uint32_t(replayFadeDuration),true,now);
      }
    }
  }else if(systemMenu) {
    systemMenu->BeginFrame(contacts,scenarioModalInput,now);
    const auto action=systemMenu->Update(systemMenuInput);
    if(action) {
      systemMenu.reset();previousMenuAction=action;
      if(action==1 || action==2)scenarioSlots=std::make_unique<PcSlotOwner>(vm->State(),resources,
          renderer,fontTextures,font,scenarioUiAudio,root,progressRoot,*slotProgress,
          scenarioRepeat,inputEnvironment.HoldFrames,now,action==1?0:1,
          [&]{return vm->CaptureWorkingRecord();});
      else if(action==3)scenarioOptions=std::make_unique<PcOptionsOwner>(vm->State(),resources,
          renderer,fontTextures,font,playback,scenarioUiAudio,scenarioAttenuation,music,sounds,
          root,progressRoot,*slotProgress,scenarioRepeat,inputEnvironment.HoldFrames,
          scenarioAudioClock,[&](uint8_t mode){scenarioMenuVoices.Load(mode);},
          [&](uint32_t id){sessionTrophies.insert(int32_t(id));},now);
      else if(action==4)scenarioBacklog=std::make_unique<PcBacklogOwner>(vm->State(),resources,renderer,
          fontTextures,font,scenarioUiAudio,*voiceOwner,root,scenarioRepeat,inputEnvironment.HoldFrames);
      else if(action==5)beginReturnTitle(now);
      else {previousMenuAction=0;sceneInput.CancelPointer();}
      return;
    }
  }else if(scenarioSlots) {
    scenarioSlots->BeginFrame(scenarioSlotInput,scenarioModalInput,contacts,now);
    const auto result=scenarioSlots->Update(now);
    if(result) {
      if(result==3) {
        if(!transfer || !scenarioSlots->PendingLoad)throw FormatError("scenario Load has no outer transfer owner");
        transfer->Load=std::move(scenarioSlots->PendingLoad);
        vm->State().SystemProgress=SnapshotSystemOwner(vm->State(),registeredHistory);
        transfer->Owner=std::make_unique<SemanticState>(std::move(vm->State()));
        scenarioSlots.reset();quit=true;return;
      }
      scenarioSlots.reset();openSystemMenu();return;
    }
  }else if(scenarioOptions) {
    scenarioOptions->BeginFrame(scenarioOptionsInput,scenarioModalInput,contacts,now);
    if(scenarioOptions->Update(now)) {
      scenarioOptions.reset();voiceSettings=*vm->State().VoiceSettings;voiceOwner->Settings=voiceSettings;voices.SetVolume(voiceSettings.Volume);
      backend.SetPreferences(*vm->State().TextSpeed,vm->State().AutoSpeed);
      opacity=*vm->State().WindowOpacity;
      openSystemMenu();return;
    }
  }else if(scenarioBacklog) {
    scenarioBacklog->BeginFrame(scenarioBacklogInput,scenarioModalInput,contacts,now);
    const auto result=scenarioBacklog->Update(now);
    if(result==2) {
      if(!transfer)throw FormatError("backlog rewind has no outer restore owner");
      transfer->Load=StageNativeBacklogLoad(vm->State().Backlog,size_t(scenarioBacklog->Selected()));
      vm->State().SystemProgress=SnapshotSystemOwner(vm->State(),registeredHistory);
      transfer->Owner=std::make_unique<SemanticState>(std::move(vm->State()));
      scenarioBacklog.reset();quit=true;return;
    }
    if(result==1){scenarioBacklog.reset();openSystemMenu();return;}
  }
  if(returningTitle) {
    titleReturnFade.Tick(now);
    if(titleReturnFade.Finished()) {
      if(!transfer)throw FormatError("return to title has no outer scene owner");
      vm->State().SystemProgress=SnapshotSystemOwner(vm->State(),registeredHistory);
      transfer->Owner=std::make_unique<SemanticState>(std::move(vm->State()));
      transfer->Title=true;quit=true;return;
    }
  }
  if(returningReplay){
    replayReturnFade.Tick(now);
    if(replayReturnFade.Finished()){transfer->Replay=true;quit=true;return;}
  }
  scenarioAttenuation.Update(now);
  playback.Update(float(now-previous)/1000);previous=now;
  if(capture){std::array<int16_t,1536> samples{};playback.Render(samples);}
  int width=0,height=0;SdlCheck(SDL_GetWindowSizeInPixels(window.Window,&width,&height),"modal viewport");
  glViewport(0,0,width,height);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
  const float scale=std::min(float(width)/960,float(height)/544);
  const std::array<GLint,4> viewport{GLint((width-960*scale)/2),GLint((height-544*scale)/2),GLint(960*scale),GLint(544*scale)};
  glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);sceneThumbnail.Present(viewport);
  if(systemMenu)systemMenu->Draw(vm->State().ChapterId,vm->State().Titles.empty()?u"":vm->State().Titles[0]);
  if(scenarioSlots)scenarioSlots->Draw();if(scenarioOptions)scenarioOptions->Draw();
  if(scenarioBacklog)scenarioBacklog->Draw();
  if(replayQuestion.Visible){
    endingLayer.Draw(replayQuestion,replayQuestionPrompt,replayQuestionLayout,1,{255,255,255,90});
    replayQuestionContacts.Latch(replayQuestion,replayQuestionLayout,endingLayer.Environment);
  }
  if(returningReplay){
    UiRenderCommand fade;fade.Kind=UiRenderKind::Rectangle;fade.Rectangle={0,0,960,544};
    fade.Color={255,255,255,uint8_t(replayReturnFade.Value.Current)};renderer.Execute(std::span<const UiRenderCommand>(&fade,1));
  }
  if(captureBacklogImage){Capture(captureDirectory/"backlog.bmp",width,height);captureBacklogImage=false;}
  if(returningTitle) {
    UiRenderCommand fade;fade.Kind=UiRenderKind::Rectangle;fade.Rectangle={0,0,960,544};
    fade.Color={0,0,0,uint8_t(titleReturnFade.Value.Current)};
    renderer.Execute(std::span<const UiRenderCommand>(&fade,1));
  }
  SdlCheck(SDL_GL_SwapWindow(window.Window),"scenario modal swap");
};

// One bounded real-service witness: normal New Game, actual menu input, Save,
// acknowledgement, menu Load, then the ordinary FromNativeSlot owner handoff.
// Only SDL events are supplied; no controller phase/result/cursor is assigned.
uint32_t scenarioCaptureStage=0;
int32_t initialBacklogScroll=-1;
const auto captureScenarioMenuInput=[&](uint64_t now) {
  const auto key=[&](SDL_Keycode code,uint32_t next) {
    for(const auto type:{SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP}) {
      SDL_Event e{};e.type=type;e.key.windowID=SDL_GetWindowID(window.Window);
      e.key.key=code;e.key.down=type==SDL_EVENT_KEY_DOWN;
      SdlCheck(SDL_PushEvent(&e),"scenario Save/Load capture input");
    }
    events<<"{\"kind\":\"scenario_menu_input\",\"stage\":"<<scenarioCaptureStage
          <<",\"key\":"<<code<<",\"now\":"<<now<<"}\n";
    scenarioCaptureStage=next;
  };
  switch(scenarioCaptureStage) {
    case 0:
      if(backend.CursorVisible() && sceneThumbnail.Ready() && vm->Opcode()==0x64){
        if(captureMenuInput==21 && vm->State().Backlog.Count()<5)key(SDLK_RETURN,0);
        else key(SDLK_F13,1);
      }
      break;
    case 1:
      if(systemMenu && systemMenu->Phase()==5) {
        if(captureMenuInput==20) {
          if(systemMenu->Selected()!=4)key(SDLK_DOWN,1);else key(SDLK_RETURN,20);
        }else if(captureMenuInput==21){
          if(systemMenu->Selected()!=3)key(SDLK_DOWN,1);else key(SDLK_RETURN,30);
        }else key(SDLK_RETURN,2);
      }
      break;
    case 2:
      if(scenarioSlots && scenarioSlots->State.Phase==4)key(SDLK_RETURN,3);
      break;
    case 3:
      if(scenarioSlots && scenarioSlots->Question().Visible && scenarioSlots->Question().Phase==3) {
        if(scenarioSlots->Question().Selected!=1)key(SDLK_RIGHT,3);
        else key(SDLK_RETURN,4);
      }
      break;
    case 4:
      if(scenarioSlots && scenarioSlots->SaveState().Phase==19 &&
          scenarioSlots->SaveModal().Visible && scenarioSlots->SaveModal().Phase==3)key(SDLK_RETURN,5);
      break;
    case 5:
      if(scenarioSlots && scenarioSlots->State.Phase==4) {
        const auto pair=ReadProgressStore(progressRoot);
        if(!pair || pair->Slots.Encode()!=slotProgress->Encode() ||
            pair->System.Encode()!=SnapshotSystemOwner(vm->State(),registeredHistory).Encode())
          throw FormatError("scenario Save did not publish its complete system/slot pair");
        const auto& saved=pair->Slots.Records.at(pair->System.Word60318);
        if(!saved.Bytes2b6[2] || !saved.ThumbnailPresent || !saved.Thumbnail || saved.Strings[2].empty())
          throw FormatError("scenario Save lost its occupied record, thumbnail or preview");
        std::ofstream summary(captureDirectory/"menu-save-load.json");
        summary<<"{\"save_verified\":true,\"paired_write\":true,\"separate_acknowledgement\":true,"
               <<"\"script\":"<<vm->State().ScriptFileId<<",\"command\":"<<vm->State().CurrentCommandIndex
               <<",\"slot\":"<<pair->System.Word60318<<",\"thumbnail_bytes\":"<<saved.Thumbnail->Pixels.size()
               <<",\"generation\":\""<<pair->Generation<<"\"}\n";
        summary.close();if(!summary)throw FormatError("cannot save scenario integration summary");
        key(SDLK_ESCAPE,6);
      }
      break;
    case 6:
      if(systemMenu && systemMenu->Phase()==5) {
        if(systemMenu->Selected()!=1)key(SDLK_DOWN,6);else key(SDLK_RETURN,7);
      }
      break;
    case 7:
      if(scenarioSlots && scenarioSlots->State.Phase==4)key(SDLK_RETURN,8);
      break;
    case 8:
      if(scenarioSlots && scenarioSlots->Question().Visible && scenarioSlots->Question().Phase==3) {
        if(scenarioSlots->Question().Selected!=1)key(SDLK_RIGHT,8);else key(SDLK_RETURN,9);
      }
      break;
    case 9:
      if(scenarioSlots && scenarioSlots->State.OperationPhase==3 &&
          scenarioSlots->Question().Visible && scenarioSlots->Question().Phase==3)key(SDLK_RETURN,10);
      break;
    case 20:
      if(systemMenu && systemMenu->Question().Visible && systemMenu->Question().Phase==3) {
        if(systemMenu->Question().Selected!=1)key(SDLK_RIGHT,20);else key(SDLK_RETURN,21);
      }
      break;
    case 21:
      if(systemMenu && systemMenu->Question().Visible && systemMenu->Question().Phase==3 &&
          systemMenu->Question().ButtonCount==3)key(SDLK_RETURN,22); // Return without a system write.
      break;
    case 30:
      if(scenarioBacklog && scenarioBacklog->Phase()==2 && !scenarioBacklog->Scrolling()){
        if(initialBacklogScroll<0)initialBacklogScroll=scenarioBacklog->FirstVisible();
        if(scenarioBacklog->Selected()>1)key(SDLK_UP,30);else key(SDLK_SPACE,31);
      }
      break;
    case 31:
      if(scenarioBacklog && scenarioBacklog->VoicePlaying(size_t(scenarioBacklog->Selected()))){
        if(scenarioBacklog->FirstVisible()>=initialBacklogScroll || scenarioBacklog->ReplayStarts!=1)
          throw FormatError("backlog capture did not scroll and start its physical voice");
        captureBacklogImage=true;
        const auto& record=vm->State().Backlog.At(size_t(scenarioBacklog->Selected())).Record;
        const auto context=DecodeNativeContextSave(record.Bytesad8);
        std::ofstream report(captureDirectory/"backlog-interaction.json");
        report<<"{\"history_count\":"<<vm->State().Backlog.Count()<<",\"selected\":"<<scenarioBacklog->Selected()
          <<",\"initial_scroll\":"<<initialBacklogScroll<<",\"scroll\":"<<scenarioBacklog->FirstVisible()
          <<",\"replay_starts\":"<<scenarioBacklog->ReplayStarts<<",\"voice_playing\":true,\"rewind_file\":"<<context.File
          <<",\"rewind_command\":"<<context.ResumeCommand<<",\"before_script\":"<<vm->State().ScriptFileId
          <<",\"before_command\":"<<vm->State().CurrentCommandIndex<<"}\n";
        report.close();if(!report)throw FormatError("cannot save backlog interaction report");
        key(SDLK_SPACE,32);
      }
      break;
    case 32:
      if(scenarioBacklog && !scenarioBacklog->VoicePlaying(size_t(scenarioBacklog->Selected())))key(SDLK_RETURN,33);
      break;
    case 33:
      if(scenarioBacklog && scenarioBacklog->Question().Visible && scenarioBacklog->Question().Phase==3)key(SDLK_RETURN,34);
      break;
  }
};

// One actual-input visibility lifecycle, using the normal title/New Game owner.
// Native polling is non-consuming: hide toggle preserves the message; menu and
// confirm wake actions also retain their ordinary action in that same frame.
uint32_t visibilityCaptureStage=0;
std::pair<int32_t,int32_t> visibilityOrigin{-1,-1};
const auto captureMessageVisibility=[&](uint64_t now) {
  const auto site=std::pair{vm->State().ScriptFileId,vm->State().CurrentCommandIndex};
  const auto key=[&](SDL_Keycode code,uint32_t next) {
    for(const auto type:{SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP}) {
      SDL_Event e{};e.type=type;e.key.windowID=SDL_GetWindowID(window.Window);
      e.key.key=code;e.key.down=type==SDL_EVENT_KEY_DOWN;
      SdlCheck(SDL_PushEvent(&e),"message visibility input");
    }
    events<<"{\"kind\":\"message_visibility_input\",\"stage\":"<<visibilityCaptureStage
      <<",\"now\":"<<now<<",\"key\":"<<code<<",\"hidden\":"<<vm->State().MessageHidden
      <<",\"ready\":"<<vm->State().ScenarioInputReady<<"}\n";
    visibilityCaptureStage=next;
  };
  if(visibilityCaptureStage>0 && visibilityCaptureStage<7 && site!=visibilityOrigin)
    throw FormatError("hide toggle/menu changed the retained message");
  switch(visibilityCaptureStage) {
    case 0:
      if(vm->Opcode()==0x64 && backend.CurrentMessage() && backend.CurrentMessage()->Reveal.Started &&
          !backend.CurrentMessage()->Reveal.Revealed && !backend.CursorVisible() && sceneThumbnail.Ready()) {
        visibilityOrigin=site;key(SDLK_SPACE,9);
      }
      break;
    case 9:
      if(site!=visibilityOrigin || vm->State().MessageHidden)
        throw FormatError("unrevealed input2 advanced or hid the message");
      if(backend.CursorVisible())key(SDLK_SPACE,1);
      break;
    case 1:
      if(!vm->State().MessageHidden || vm->State().ScenarioInputReady || skipAuto.Auto || vm->State().SkipMode)
        throw FormatError("hide toggle did not apply native readiness/policy");
      key(SDLK_SPACE,2);break;
    case 2:
      if(vm->State().MessageHidden || vm->State().ScenarioInputReady)
        throw FormatError("nonadvancing show toggle differs");
      key(SDLK_SPACE,3);break;
    case 3:
      if(!vm->State().MessageHidden)throw FormatError("second hide did not take effect");
      key(SDLK_F13,4);break;
    case 4:
      if(systemMenu && systemMenu->Phase()==5) {
        if(vm->State().MessageHidden)throw FormatError("menu wake did not restore text visibility");
        key(SDLK_ESCAPE,5);
      }
      break;
    case 5:
      if(!modalOwned())key(SDLK_SPACE,6);
      break;
    case 6:
      if(!vm->State().MessageHidden)throw FormatError("confirmation wake needs an actually hidden message");
      key(SDLK_RETURN,7);break;
    case 7:
      if(vm->Opcode()==0x64 && site!=visibilityOrigin) {
        if(vm->State().MessageHidden || modalOwned())throw FormatError("confirmation did not restore and advance");
        std::ofstream result(captureDirectory/"message-visibility.json");
        result<<"{\"passed\":true,\"origin\":["<<visibilityOrigin.first<<','<<visibilityOrigin.second
          <<"],\"resumed\":["<<site.first<<','<<site.second<<"],\"now\":"<<now
          <<",\"unrevealed_input2_reveals_only\":true,\"toggle_preserves_message\":true,\"toggle_clears_policy_and_readiness\":true,\"hidden_menu_wake_opens_menu\":true,\"cancel_preserves_message\":true,\"hidden_confirm_wake_advances\":true,\"native_nonconsuming_input\":true,\"manual_visual_acceptance\":false}\n";
        result.close();if(!result)throw FormatError("cannot save message visibility result");
        targetCompleted=true;quit=true;visibilityCaptureStage=8;
      }
      break;
  }
};
