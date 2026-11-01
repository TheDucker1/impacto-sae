// Bounded interaction witnesses through the normal title and SDL input.
// Observations drive button timing; no controller phase, readiness, history or
// service result is assigned by this automation.
struct ExtrasInteractionCapture {
  uint8_t Mode=1;int32_t FirstBuffer=-1,SecondBuffer=-1;
  int Stage=0;int32_t Row=-1,History=-1;uint64_t PausedAt=0,PausePosition=0,ResumePosition=0;
  Bytes ExpectedSystem;bool Finished=false;
  bool OpenReady=false;uint64_t MovieVideo=0,MovieAudio=0;
  std::filesystem::path ReplayOutput;
  template<class Key>void Tick(PcTitleOwner& title,SemanticState& owner,MusicPlayback& playback,
      const ExtrasScreenEnvironment& env,uint64_t now,Key key){
    if(Stage==0 && title.State.Phase==10 && !title.Extras()){
      if(title.State.Selected<3)key(SDLK_DOWN);else {key(SDLK_RETURN);Stage=1;}return;
    }
    const auto* extras=title.Extras();
    if(Stage==1 && extras && extras->State().Phase()==2 && extras->State().Fade()==0){
      const auto target=Mode==1?2:Mode==3?1:Mode==4?3:0;
      if(extras->State().Selected()<target)key(SDLK_DOWN);else {key(SDLK_RETURN);Stage=2;}return;
    }
    if(Mode==2){TickCg(title,owner,playback,key);return;}
    if(Mode==3){TickMovie(title,owner,key);return;}
    if(Mode==4){TickBattle(title,playback,key);return;}
    const auto* music=extras?extras->MusicChild():nullptr;
    if(Stage==2 && music && music->State().Phase()==3){
      const auto& state=music->State();
      if(state.Status(size_t(state.Selected()))!=1){
        if(size_t(state.Selected()+1)>=env.MusicTracks.size())throw FormatError("Extras witness has no newly unlocked authored music row");
        key(SDLK_DOWN);return;
      }
      Row=state.Selected();History=env.MusicTracks.at(size_t(Row)).History;key(SDLK_RETURN);Stage=3;return;
    }
    if(Stage==3 && music && music->State().PlayingRow()==Row && playback.BgmPositionMs()>0){key(SDLK_RETURN);Stage=4;return;}
    if(Stage==4 && playback.BgmPaused()){PausedAt=now;PausePosition=playback.BgmPositionMs();Stage=5;return;}
    if(Stage==5 && now-PausedAt>=64){
      if(!playback.BgmBusy() || !playback.BgmPaused() || playback.BgmPositionMs()!=PausePosition)
        throw FormatError("Extras pause did not retain its live audio source/position");
      key(SDLK_RETURN);Stage=6;return;
    }
    if(Stage==6 && !playback.BgmPaused() && playback.BgmPositionMs()>PausePosition){
      ResumePosition=playback.BgmPositionMs();key(SDLK_ESCAPE);Stage=7;return;
    }
    if(Stage==7 && extras && extras->State().Phase()==7 && extras->Question().Visible && extras->Question().Phase==3){
      if(extras->Question().Selected!=1){key(SDLK_RIGHT);return;}
      if(ExactMusicStatus(owner.MusicHistory,size_t(History))!=3)throw FormatError("Extras music play did not consume new history");
      ExpectedSystem=SnapshotSystemOwner(owner,RestoreSystemReadHistory(*owner.SystemProgress)).Encode();
      key(SDLK_RETURN);Stage=8;return;
    }
    if(Stage==8 && extras && extras->Save().State().Phase==19 && extras->Save().Modal.Visible && extras->Save().Modal.Phase==3){key(SDLK_RETURN);Stage=9;return;}
    if(Stage==9 && extras && extras->State().Phase()==2 && extras->State().Fade()==0){key(SDLK_ESCAPE);Stage=10;return;}
    if(Stage==10 && !extras && title.State.Phase==10){Finished=true;}
  }
  template<class Key>void TickCg(PcTitleOwner& title,SemanticState& owner,MusicPlayback& playback,Key key){
    const auto* extras=title.Extras();const auto* cg=extras?extras->CgChild():nullptr;
    if(Stage==2 && cg && cg->State().Phase()==4){
      const auto& state=cg->State();
      for(size_t i=0;i<state.Catalogue().Cards.size();++i){const auto variants=state.Variants(i);
        if(std::find(variants.begin(),variants.end(),true)!=variants.end()){Row=int32_t(i);break;}}
      if(Row<0)throw FormatError("CG interaction requires an unlocked image in imported progress");
      key(SDLK_RETURN);Stage=3;return;
    }
    if(Stage==3 && cg && cg->State().Phase()==5 && !cg->State().List().Scrolling){
      const auto& state=cg->State();
      if(state.List().Rows.Selected<Row/9)key(SDLK_E);
      else if(state.Cell()<Row%9)key(SDLK_RIGHT);
      else {key(SDLK_RETURN);Stage=4;}return;
    }
    const bool visible=cg && cg->State().Viewing() && cg->State().Phase()==9 && cg->State().ImageOpacity()==255 && cg->State().PreviousOpacity()==0;
    if(Stage==4 && visible){const auto& state=cg->State();FirstBuffer=state.CurrentBuffer();
      History=state.Catalogue().Card(size_t(Row))->Images.at(size_t(state.Variant()));key(SDLK_C);Stage=5;return;}
    if(Stage==5 && visible && playback.OrdinaryVoicePlaying(7) && playback.OrdinaryVoicePosition(7)>0){
      PausePosition=uint64_t(playback.OrdinaryVoicePosition(7));key(SDLK_ESCAPE);Stage=6;return;}
    if(Stage==6 && visible && !cg->State().CommentActive() && !playback.OrdinaryVoicePlaying(7)){key(SDLK_ESCAPE);Stage=7;return;}
    if(Stage==7 && cg && cg->State().Phase()==5){key(SDLK_RETURN);Stage=8;return;}
    if(Stage==8 && visible){SecondBuffer=cg->State().CurrentBuffer();
      if(FirstBuffer<0 || SecondBuffer<0 || FirstBuffer==SecondBuffer)throw FormatError("CG reopen did not alternate its prepared image buffer");
      key(SDLK_ESCAPE);Stage=9;return;}
    if(Stage==9 && cg && cg->State().Phase()==5){key(SDLK_ESCAPE);Stage=10;return;}
    if(Stage==10 && cg && cg->State().Phase()==4){key(SDLK_ESCAPE);Stage=11;return;}
    if(Stage==11 && extras && extras->State().Phase()==7 && extras->Question().Visible && extras->Question().Phase==3){
      if(extras->Question().Selected!=0)throw FormatError("CG return question lost its native initial selection");
      if(std::get<int32_t>(CgHistoryStatus(owner.CgHistory,History))!=3)throw FormatError("CG viewing did not consume new image history");
      const auto& h=owner.SystemProgress->Histories[7];const auto status=size_t(Row)<h.Values.size()?h.Values[size_t(Row)]:h.Fallback;
      if(status!=1 && status!=2 && status!=3)throw FormatError("CG commentary did not mark its catalogue card");
      key(SDLK_RETURN);Stage=12;return;
    }
    if(Stage==12 && extras && extras->State().Phase()==2 && extras->State().Fade()==0){key(SDLK_ESCAPE);Stage=13;return;}
    if(Stage==13 && !extras && title.State.Phase==10)Finished=true;
  }
  template<class Key>void TickMovie(PcTitleOwner& title,SemanticState& owner,Key key){
    const auto* extras=title.Extras();const auto* movie=extras?extras->MovieChild():nullptr;
    if(Stage==2 && movie && movie->State().Phase()==5 && movie->PlaybackState().VideoPresented){
      if(!movie->State().Unlocked(0) || movie->State().Unlocked(1))throw FormatError("movie witness requires original unlocked0/locked1 progress");
      key(SDLK_RIGHT);Stage=3;return;
    }
    if(Stage==3 && movie && movie->State().Selected()==1 && !movie->State().List().Scrolling){key(SDLK_LEFT);Stage=4;return;}
    if(Stage==4 && movie && movie->State().Selected()==0 && !movie->State().List().Scrolling){
      OpenReady=movie->MovieReady();key(SDLK_RETURN);Stage=5;return;
    }
    if(Stage==5 && movie && movie->State().Phase()==7 && movie->State().MovieRectangle()==std::array<int32_t,4>{0,0,960,544} &&
        movie->PlaybackState().VideoPresented>=3 && movie->PlaybackState().AudioPlayed>0){
      MovieVideo=movie->PlaybackState().VideoPresented;MovieAudio=movie->PlaybackState().AudioPlayed;
      key(SDLK_RETURN);Stage=6;return;
    }
    if(Stage==6 && movie && movie->State().Phase()==5 && movie->State().Browsing()){
      if(movie->State().MovieRectangle()==std::array<int32_t,4>{0,0,960,544})throw FormatError("movie return retained full-view geometry");
      key(SDLK_ESCAPE);Stage=7;return;
    }
    if(Stage==7 && extras && extras->State().Phase()==7 && extras->Question().Visible && extras->Question().Phase==3){
      const auto& viewed=owner.SystemProgress->Histories[4];const auto& played=owner.SystemProgress->Histories[5];
      if(extras->Question().Selected!=0 || viewed.Values.empty() || viewed.Values[0]!=3 || played.Values.empty() || played.Values[0]!=1)
        throw FormatError("movie open lost its view/played histories or return question");
      key(SDLK_RETURN);Stage=8;return;
    }
    if(Stage==8 && extras && extras->State().Phase()==2 && extras->State().Fade()==0){key(SDLK_ESCAPE);Stage=9;return;}
    if(Stage==9 && !extras && title.State.Phase==10)Finished=true;
  }
  template<class Key>void TickBattle(PcTitleOwner& title,MusicPlayback& playback,Key key){
    const auto* extras=title.Extras();auto* battle=extras?extras->BattleChild():nullptr;
    if(Stage==2 && battle && battle->State().Phase()==5 && !battle->State().List().Scrolling){
      if(!battle->State().Unlocked(size_t(battle->State().Selected()))){
        if(battle->State().Selected()==battle->State().List().Rows.Maximum)throw FormatError("replay capture requires unlocked imported battle history");
        key(SDLK_DOWN);return;
      }
      Row=battle->State().Selected();battle->CaptureReplay(ReplayOutput);key(SDLK_RETURN);Stage=3;return;
    }
    if(Stage==3 && battle && battle->Question().Visible && battle->Question().Phase==3){
      if(battle->Question().Selected!=1)key(SDLK_RIGHT);else {key(SDLK_RETURN);Stage=4;}return;
    }
    if(Stage==4 && battle && battle->SessionSerial()==1 && battle->State().Phase()==5){
      if(battle->State().Selected()!=Row || !playback.BgmBusy())throw FormatError("replay lost its browser selection or restored Extras music");
      key(SDLK_ESCAPE);Stage=5;return;
    }
    if(Stage==5 && extras && extras->State().Phase()==7 && extras->Question().Visible && extras->Question().Phase==3){key(SDLK_RETURN);Stage=6;return;}
    if(Stage==6 && extras && extras->State().Phase()==2 && extras->State().Fade()==0){key(SDLK_ESCAPE);Stage=7;return;}
    if(Stage==7 && !extras && title.State.Phase==10)Finished=true;
  }
  void Verify(const std::filesystem::path& root,const std::filesystem::path& output,
      const ProgressPair& initial,const SemanticState& owner,size_t frames)const{
    const auto saved=ReadProgressStore(root);
    if(Mode==2 || Mode==3 || Mode==4){
      if(!Finished || !saved || saved->Generation!=initial.Generation || saved->System.Encode()!=initial.System.Encode() || saved->Slots.Encode()!=initial.Slots.Encode())
        throw FormatError("Extras no-save return changed its imported progress pair");
      if(Mode==4){
        if(!std::filesystem::exists(ReplayOutput/"replay.json"))throw FormatError("battle capture missed its actual nested session report");
        std::ofstream report(output/"extras.json");report<<"{\"passed\":true,\"interaction\":\"title-extras-battle-replay-decline-abort-return-no-save\",\"frames\":"<<frames
          <<",\"row\":"<<Row<<",\"replay_sessions\":1,\"selection_and_music_restored\":true,\"disk_pair_unchanged\":true,\"manual_acceptance\":false}\n";
        if(!report)throw FormatError("cannot write battle interaction report");return;
      }
      if(Mode==3){
        std::ofstream report(output/"extras.json");report<<"{\"passed\":true,\"interaction\":\"title-extras-movie-select-open-readiness-view-return-no-save\",\"frames\":"<<frames
          <<",\"movie\":0,\"ready_before_open\":"<<(OpenReady?"true":"false")<<",\"video_frames\":"<<MovieVideo<<",\"audio_frames\":"<<MovieAudio
          <<",\"full_view_geometry\":true,\"disk_pair_unchanged\":true,\"manual_acceptance\":false}\n";
        if(!report)throw FormatError("cannot write movie interaction report");return;
      }
      std::ofstream report(output/"extras.json");report<<"{\"passed\":true,\"interaction\":\"title-extras-cg-comment-interrupt-reopen-return-no-save\",\"frames\":"<<frames
        <<",\"card\":"<<Row<<",\"image\":"<<History<<",\"comment_position_ms\":"<<PausePosition<<",\"image_buffers\":["<<FirstBuffer<<','<<SecondBuffer
        <<"],\"comment_interruption_retains_viewer\":true,\"disk_pair_unchanged\":true,\"manual_acceptance\":false}\n";
      if(!report)throw FormatError("cannot write CG interaction report");return;
    }
    if(!Finished || ExpectedSystem.empty() || !saved || saved->Generation==initial.Generation ||
       saved->System.Encode()!=ExpectedSystem || saved->Slots.Encode()!=initial.Slots.Encode() ||
       SnapshotSystemOwner(owner,RestoreSystemReadHistory(*owner.SystemProgress)).Encode()!=ExpectedSystem)
      throw FormatError("Extras title/music/return/save witness lost persistent owner or paired slots");
    std::ofstream report(output/"extras.json");
    report<<"{\"passed\":true,\"interaction\":\"title-extras-music-pause-resume-return-save-title\",\"frames\":"<<frames
      <<",\"row\":"<<Row<<",\"history_id\":"<<History<<",\"paused_position_ms\":"<<PausePosition<<",\"resumed_position_ms\":"<<ResumePosition
      <<",\"complete_system_snapshot_matches\":true,\"paired_slots_preserved\":true,\"new_generation\":true,\"manual_acceptance\":false}\n";
    if(!report)throw FormatError("cannot write Extras interaction report");
  }
};
