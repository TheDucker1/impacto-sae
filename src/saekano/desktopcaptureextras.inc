// One bounded interaction witness through the normal title and SDL input.
// Observations drive button timing; no controller phase, readiness, history or
// service result is assigned by this automation.
struct ExtrasMusicCapture {
  int Stage=0;int32_t Row=-1,History=-1;uint64_t PausedAt=0,PausePosition=0,ResumePosition=0;
  Bytes ExpectedSystem;bool Finished=false;
  template<class Key>void Tick(PcTitleOwner& title,SemanticState& owner,MusicPlayback& playback,
      const ExtrasScreenEnvironment& env,uint64_t now,Key key){
    if(Stage==0 && title.State.Phase==10 && !title.Extras()){
      if(title.State.Selected<3)key(SDLK_DOWN);else {key(SDLK_RETURN);Stage=1;}return;
    }
    const auto* extras=title.Extras();
    if(Stage==1 && extras && extras->State().Phase()==2 && extras->State().Fade()==0){
      if(extras->State().Selected()<2)key(SDLK_DOWN);else {key(SDLK_RETURN);Stage=2;}return;
    }
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
  void Verify(const std::filesystem::path& root,const std::filesystem::path& output,
      const ProgressPair& initial,const SemanticState& owner,size_t frames)const{
    const auto saved=ReadProgressStore(root);
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
