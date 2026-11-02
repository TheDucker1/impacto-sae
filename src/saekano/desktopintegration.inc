// Bounded automation of the playable scene's existing owners. The input packet
// contains native initial context and actions, never expected observations.
struct SceneIntegrationCapture {
  struct Action { int32_t Script,Command,Opcode,Kind,A,B;bool Applied=false; };
  NativePendingSlotLoad Initial;
  std::array<int16_t,80> Bank1{};
  std::array<uint8_t,18> ApproachFlags{};
  int32_t Otaku=0,ApproachGauge=0,BattleGauge=0;
  bool ApproachEnabled=false;int32_t ApproachId=0;
  int32_t SkipMode=0,SkipUnread=0;
  size_t Limit=0,Completions=0;std::vector<Action> Actions;bool Ready=false,Done=false;
  std::ofstream Trace;
  Action* ActiveProfile=nullptr;
  uint64_t LastProgress=SDL_GetTicks();
  SceneIntegrationCapture(const std::filesystem::path& directory,const std::filesystem::path& output) {
    const auto bytes=SceneRead(directory/"input.bin");PayloadReader r(bytes);
    if(r.U32()!=0x32494753)throw FormatError("invalid scene integration packet");
    Limit=r.U32();if(!Limit || Limit>100000)throw FormatError("invalid integration command bound");
    const auto size=r.U32();r.CheckCount(size,1);Bytes record;record.reserve(size);
    for(uint32_t i=0;i<size;++i)record.push_back(r.U8());
    Initial.Record=NativeSlotRecord::Decode(record);Initial.Context=DecodeNativeContextSave(Initial.Record.Bytesad8);
    Initial.FromBacklog=true; // Checkpoint envelope, not an authored occupied slot.
    if(Initial.Context.File!=int32_t(Initial.Record.Words1ad8[0]) || Initial.Context.ResumeCommand!=int32_t(Initial.Record.Words1ad8[1]))
      throw FormatError("checkpoint header/context disagree");
    for(auto& value:Bank1)value=std::bit_cast<int16_t>(r.U16());for(auto& value:ApproachFlags)value=r.U8();
    Otaku=r.I32();ApproachGauge=r.I32();BattleGauge=r.I32();ApproachEnabled=r.U8()!=0;ApproachId=r.I32();
    SkipMode=r.I32();SkipUnread=r.I32();
    const auto count=r.U32();r.CheckCount(count,24);
    for(uint32_t i=0;i<count;++i)Actions.push_back({r.I32(),r.I32(),r.I32(),r.I32(),r.I32(),r.I32()});
    if(r.Remaining())throw FormatError("trailing integration input");
    Trace.open(output/"integration.jsonl");if(!Trace)throw FormatError("cannot open integration trace");
  }
  void SeedOwner(SemanticState& owner)const {owner.Bank1=Bank1;owner.OtakuMode=Otaku;}
  void Observe(const VirtualMachine& vm,const char* event,uint16_t opcode) {
    const auto& s=vm.State();
    Trace<<"{\"event\":\""<<event<<"\",\"script\":"<<s.ScriptFileId<<",\"command\":"<<s.CurrentCommandIndex
      <<",\"opcode\":"<<opcode<<",\"pending_command\":"<<s.PendingCommandIndex<<",\"pending_file\":"<<s.PendingFileId
      <<",\"chapter_id\":"<<s.ChapterId<<",\"day_enabled\":"<<s.DayEnabled<<",\"time_components\":["<<s.TimeComponents[0]<<','<<s.TimeComponents[1]
      <<"],\"otaku_mode\":"<<s.OtakuMode<<",\"approach_gauge\":"<<s.ApproachGauge<<",\"battle_gauge\":"<<s.BattleGauge<<",\"bank0\":[";
    for(size_t i=0;i<s.Bank0.size();++i){if(i)Trace<<',';Trace<<s.Bank0[i];}
    Trace<<"],\"bank1\":[";for(size_t i=0;i<s.Bank1.size();++i){if(i)Trace<<',';Trace<<s.Bank1[i];}
    Trace<<"],\"approach_flags\":[";for(size_t i=0;i<s.ApproachFlags.size();++i){if(i)Trace<<',';Trace<<unsigned(s.ApproachFlags[i]);}
    Trace<<"],\"lane_command_index\":[";for(size_t i=0;i<s.LaneCommandIndex.size();++i){if(i)Trace<<',';Trace<<s.LaneCommandIndex[i];}
    Trace<<"],\"speaker_id\":"<<s.SpeakerId<<",\"messages_units\":[";
    const auto units=[&](std::u16string_view value){Trace<<'[';for(size_t i=0;i<value.size();++i){if(i)Trace<<',';Trace<<uint16_t(value[i]);}Trace<<']';};
    for(size_t side=0;side<2;++side){if(side)Trace<<',';Trace<<'[';for(size_t i=0;i<s.Messages[side].size();++i){if(i)Trace<<',';units(s.Messages[side][i]);}Trace<<']';}
    Trace<<"],\"titles_units\":[";for(size_t i=0;i<s.Titles.size();++i){if(i)Trace<<',';units(s.Titles[i]);}
    Trace<<"],\"divergence_stack\":[";for(size_t i=0;i<s.DivergenceStack.size();++i){if(i)Trace<<',';const auto& row=s.DivergenceStack[i];Trace<<'['<<row[0]<<','<<row[1]<<','<<row[2]<<']';}
    Trace<<"]}\n";Trace.flush();
  }
  void Boundary(VirtualMachine& vm,const VmEvent& event) {
    if(event.Boundary==VmBoundary::ProfileDestinationLoaded && ActiveProfile){ActiveProfile->Applied=true;ActiveProfile=nullptr;}
    if(!Ready && event.Boundary==VmBoundary::SlotFileReady) {
      auto& state=vm.State();state.ApproachFlags=ApproachFlags;state.OtakuMode=Otaku;
      state.ApproachGauge=ApproachGauge;state.BattleGauge=BattleGauge;
      state.SkipMode=SkipMode;state.SkipUnreadEnabled=SkipUnread;
      state.ApproachMode.Enabled=ApproachEnabled;state.ApproachMode.Id=ApproachId;
      Ready=true;Observe(vm,"native_checkpoint_restored",vm.Opcode());
    }
    if(Ready && event.Boundary==VmBoundary::CommandCompletedBeforeDispatch) {
      Observe(vm,"command_completed",event.Opcode);++Completions;LastProgress=SDL_GetTicks();
      for(auto& action:Actions)if(!action.Applied && action.Script==event.Script && action.Command==int32_t(event.Command) && action.Opcode==event.Opcode){action.Applied=true;break;}
      if(Completions>=Limit){Done=true;Trace<<"{\"event\":\"bounded_complete\",\"commands\":"<<Completions<<"}\n";Trace.flush();}
    }
  }
  uint32_t Input(VirtualMachine& vm,CommandInput& input) {
    if(!Ready || vm.Boundary()==VmBoundary::CommandCompletedBeforeDispatch)return 0;
    if(SDL_GetTicks()-LastProgress>30000 && !vm.MovieActive())throw FormatError("integration made no command progress for30seconds");
    input.Advance=true;input.Profile.Accept=true;input.Profile.Confirmation=true;
    // The normal frontend's skip/history policy owns input.Route. Restore its
    // captured policy settings rather than forcing an incompatible phase.
    const auto& state=vm.State();
    Action* chosen=nullptr;
    for(auto& action:Actions)if(!action.Applied && action.Script==state.ScriptFileId && action.Command==state.CurrentCommandIndex && action.Opcode==vm.Opcode()){chosen=&action;break;}
    if(chosen && chosen->Kind==1) {
      if(vm.Boundary()==VmBoundary::BeforeStart || vm.Boundary()==VmBoundary::FileLoadedBeforeStart || vm.Boundary()==VmBoundary::ProfileDestinationLoaded) {
        vm.SetGauge(chosen->A!=0,chosen->B);chosen->Applied=true;
        Trace<<"{\"event\":\"gauge_input\",\"script\":"<<state.ScriptFileId<<",\"command\":"<<state.CurrentCommandIndex<<",\"value\":"<<chosen->B<<"}\n";
      }
      return 0;
    }
    if(chosen && chosen->Kind==2 && vm.CurrentMovie()) {
      vm.SetMovieSkipAllowed(chosen->B!=0);chosen->Applied=true;
      Trace<<"{\"event\":\"movie_skip_eligibility_input\",\"script\":"<<state.ScriptFileId<<",\"command\":"<<state.CurrentCommandIndex<<",\"value\":"<<chosen->B<<"}\n";
    }
    if(vm.Opcode()==0x65 || vm.Opcode()==0x3f2 || vm.Opcode()==0x3f6 || vm.Opcode()==0x3ed) {
      input.Advance=false;input.Profile.Accept=false;input.Profile.Confirmation.reset();
      if(!chosen)throw FormatError("integration reached an unplanned native decision");
      if(vm.Opcode()==0x65 && state.OrdinaryChoice.Phase==4)input.Ordinary.Released=size_t(chosen->A);
      if(vm.Opcode()==0x3f6) {
        const auto& menu=state.ApproachSelection;
        if(menu.Phase==4) {
          const auto found=std::find(menu.OriginalOrdinals.begin(),menu.OriginalOrdinals.end(),size_t(chosen->A));
          if(found==menu.OriginalOrdinals.end())throw FormatError("recorded approach parent is unavailable");
          input.Approach.ParentReleased=size_t(found-menu.OriginalOrdinals.begin());
        }else if(menu.Phase==7)input.Approach.ChildReleased=size_t(chosen->B);
      }
      if(vm.Opcode()==0x3ed) {
        ActiveProfile=chosen;
        input.Profile.Ordinal=size_t(chosen->A);input.Profile.Accept=true;input.Profile.Confirmation=true;
        const auto* profile=vm.CurrentProfile();
        if(profile && profile->CurrentStage()==ProfileMode::Stage::Selection && profile->Selection().Phase==6) {
          const auto selected=profile->Selection().Selectors.at(size_t(chosen->A));
          if(selected!=uint32_t(chosen->B)){input.Profile.Accept=false;input.Profile.EventDirection=true;}
        }
      }
      if(vm.Opcode()==0x3f2 && chosen->A>0 && chosen->A<=3)return 1u<<uint32_t(chosen->A-1);
    }
    return 0;
  }
};
