// Included after the ordinary scenario runner so replay uses its actual SDL,
// VM, media and retirement lifecycle while the gallery owner is suspended.
class PcExtrasBattle final:public ExtrasBattleServices {
 public:
  PcExtrasBattle(SceneDialogLayer& shared,SemanticState& owner,PlaybackTitleAudio& audio,
      MusicPlayback& playback,ViewerWindow& window,ArchiveBgmResources& music,ArchiveSeResources& sounds,
      const std::filesystem::path& root,const std::filesystem::path& saveRoot,NativeSlotData& slots,
      std::function<void()> restoreMusic,std::span<const int32_t> repeats,uint32_t hold,bool unlockAll,uint64_t now)
      :shared_(shared),owner_(owner),audio_(audio),playback_(playback),window_(window),music_(music),sounds_(sounds),
       root_(root),saveRoot_(saveRoot),slots_(slots),restoreMusic_(std::move(restoreMusic)),hold_(hold),
       env_(ExtrasScreenEnvironment::DecodeThroughBattle(SceneRead(root/"env/env_omake.dat"))),
       help_(ExtrasChildButtonHelpEnvironment::DecodeThroughBattle(SceneRead(root/"env/env_help_mgr.dat"))),
       style_(ButtonHelpStyle::Decode(SceneRead(root/"env/env_global.dat"))),
       markerEnv_(NewMarkerEnvironment::Decode(SceneRead(root/"env/env_global.dat"))),marker_(StartNewMarker(markerEnv_,now)),
       screen_(env_,owner,*this,repeats,hold,unlockAll,now){
    if(!restoreMusic_ || help_.BattleRows.empty())throw FormatError("battle gallery help/music owner missing");
    const auto names=DecodeBattleReplayScripts(shared.Resources.LoadExtrasBattleScripts().Data);
    if(names.size()!=env_.BattleReplays.size() || shared.Resources.ExtrasBattleThumbnailCount()!=names.size())
      throw FormatError("battle gallery scripts/thumbnails disagree with authored entries");
    const auto index=ScriptNameIndex::FromFirstArchive(SceneRead(root/"first.dat"));
    for(const auto& name:names)scripts_.push_back(index.Resolve(name));
    try{
      for(size_t i=0;i<names.size();++i){const ProfileTextureKey key{ProfileTextureSection::ExtrasBattleThumbnail,-1,uint32_t(i)};
        if(!shared.Renderer.Textures.contains(key)){
          shared.Renderer.Upload(key,DecodeSceneImage(shared.Resources.LoadProfileTexture(key).Data));owned_.insert(key);
        }
      }
    }catch(...){RetireTextures();throw;}
  }
  ~PcExtrasBattle(){RetireTextures();}
  void BeginFrame(const ExtrasBattleInput& input,const DialogInput& modal,const WidgetInputHistory::Snapshot& contacts){
    input_=input;modalInput_=modal;screen_.BeginFrame(contacts);contacts_.BeginFrame(contacts);
  }
  void CancelPointer(){screen_.CancelPointer();contacts_.CancelPointer();}
  int32_t Update(uint64_t now){
    if(windowClosed_)return 0;
    now_=now;const auto result=screen_.Update(input_,now);
    if(screen_.ReplayRequested()){
      const auto started=SDL_GetTicks();std::unique_ptr<SemanticState> returned;
      const auto code=RunScenarioScene(window_,shared_.Renderer,shared_.Textures,shared_.Font,shared_.Resources,
          playback_,music_,sounds_,std::move(owner_),nullptr,root_,{},false,false,false,{},{},{},false,false,0,{},
          saveRoot_,&slots_,nullptr,0,&returned,scripts_.at(size_t(screen_.Selected())));
      if(code!=0)throw FormatError("battle replay session failed");
      if(!returned){windowClosed_=true;return 0;}
      owner_=std::move(*returned);restoreMusic_();
      now_+=SDL_GetTicks()-started;CancelPointer();input_={};modalInput_={};screen_.ResumeReplay(now_);
    }
    if(!result)UpdateNewMarker(marker_,markerEnv_,now_);return result;
  }
  bool Dirty()const{return screen_.Dirty();}
  bool WindowClosed()const{return windowClosed_;}
  const ExtrasBattleBrowser& State()const{return screen_;}
  bool ResourcesReady()const override{
    if(scripts_.size()!=env_.BattleReplays.size())return false;
    for(size_t i=0;i<scripts_.size();++i)
      if(!shared_.Renderer.Textures.contains({ProfileTextureSection::ExtrasBattleThumbnail,-1,uint32_t(i)}))return false;
    return true;
  }
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  void StopMusic()override{playback_.StopBgmImmediately();}
  void BeginConfirmation()override{
    if(question_.Visible)throw FormatError("battle replay confirmation already active");
    const auto count=shared_.Environment.ButtonGroups.at(1).size();
    if(count!=2)throw FormatError("battle replay confirmation requires two authored buttons");
    layout_=shared_.Layout(env_.Strings.at(0xde60),count);BeginDialog(question_,layout_,count,0,true,true,now_);
  }
  OptionsDialogPoll PollConfirmation()override{
    auto input=modalInput_;input.CollectActivations=[this]{return contacts_.Collect(question_,shared_.Environment,hold_,now_);};
    UpdateDialog(question_,input,now_);for(const auto id:question_.UiSounds)UiSound(id);
    return {question_.Visible,question_.Canceled,question_.Result};
  }
  void Draw(){
    ScopedSdkGlState scope;auto& r=shared_.Renderer;
    glUseProgram(r.Program);glBindVertexArray(r.Vao);glBindBuffer(GL_ARRAY_BUFFER,r.Buffer);
    glActiveTexture(GL_TEXTURE0);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glUniform1i(r.AlphaMode,0);r.SetFilter(0,0);ApplyParticleBlend(1);
    if(screen_.Phase()<=2)return;
    Rectangle(128);
    auto s=env_.Sprites.at(0xdd10);s.Color[3]=uint8_t(Word(0xd8f8));Sprite(40,s);
    s=env_.Sprites.at(0xdd38);s.Color[3]=uint8_t(Word(0xdd34));Sprite(41,s);
    Stretch({ProfileTextureSection::SystemUi,-1,62},env_.Common.SharedSprites[1],env_.Common.StretchSize);
    Sprite(62,env_.Common.SharedSprites[0]);
    const auto shared10=ProfileTextureKey{ProfileTextureSection::SharedUi,-1,10};
    shared_.Sprite(ResolveUiSprite(shared10,env_.Sprites.at(0xdafc)));
    const auto extent=env_.Points.at(0xdb44);Stretch(shared10,env_.Sprites.at(0xdb20),extent);
    shared_.Sprite(ResolveUiSprite(shared10,env_.Sprites.at(0xdb4c)));
    float pitch=float(extent[1])/float(env_.BattleReplays.size());int32_t thumb;
    if(pitch<12){thumb=env_.BattleReplays.size()==1?0:int32_t(int64_t(extent[1]-12)*screen_.Selected()/int64_t(env_.BattleReplays.size()-1));pitch=12;}
    else thumb=int32_t(pitch*float(screen_.Selected()));
    thumb+=env_.Sprites.at(0xdb20).Y;
    const auto shared11=ProfileTextureKey{ProfileTextureSection::SharedUi,-1,11};
    const auto width=env_.Points.at(0xdbdc)[0];const auto& top=env_.Sprites.at(0xdb70);const auto& bottom=env_.Sprites.at(0xdbb8);
    s=top;s.Y+=thumb;Stretch(shared11,s,{width,top.Height});
    const auto middle=int32_t(pitch-float(top.Height)-float(bottom.Height));
    s=env_.Sprites.at(0xdb94);s.Y+=thumb;Stretch(shared11,s,{width,middle});
    s=bottom;s.Y+=thumb+env_.Sprites.at(0xdb94).Y+middle;Stretch(shared11,s,{width,top.Height});
    s=env_.Sprites.at(0xddc8);s.Y-=int32_t(screen_.Arrows().Current);Sprite(57,s);
    s=env_.Sprites.at(0xde10);s.Y+=int32_t(screen_.Arrows().Current);
    auto arrow=ResolveUiSprite({ProfileTextureSection::SystemUi,-1,57},s);arrow.Source[3]=-arrow.Source[3];shared_.Sprite(arrow);
    const auto& list=screen_.List();SlotViewportInputs viewport;
    viewport.Visible=true;viewport.Animated=true;viewport.Scrolling=list.Scrolling;viewport.Rows=list.Rows;
    viewport.Rectangle=env_.Rectangles.at(0xdbe4);viewport.Pitch=list.Settings.Pitch;
    viewport.ScrollOffset=list.Scroll.Current;viewport.CursorOffset=list.Cursor.Current;
    for(const auto& command:BuildSlotViewport(viewport)){
      if(command.Kind!=SlotViewportKind::Cards){r.Execute(std::span<const UiRenderCommand>(&command.Render,1));continue;}
      const auto& cards=command.Cards;
      for(int32_t row=cards.Begin;row<cards.End;++row){
        if(row<0 || size_t(row)>=env_.BattleReplays.size())continue;
        const auto offset=Word(0xdc00)+list.Settings.Pitch*(row-cards.FirstVisible)+cards.Offsets[0];
        s=env_.Sprites.at(0xdc30);s.Alignment=0;s.Y+=offset;Sprite(42,s);
        s=env_.Sprites.at(0xdc64);s.Alignment=0;s.Y+=offset;s.Color=Color(row==cards.Selected && cards.Offsets[0]==0?0xdc88:0xdc8c);Sprite(39,s);
        const auto& entry=env_.BattleReplays[size_t(row)];const bool unlocked=screen_.Unlocked(size_t(row));
        const auto rowText=[&](std::u16string_view text,uint16_t point,uint16_t scale,std::array<uint8_t,4> color=std::array<uint8_t,4>{0,0,0,255}){
          auto p=env_.Points.at(point);p[1]+=offset;Text(text,p,FontSize(scale),false,color);
        };
        if(unlocked){rowText(entry.Text[0],0xdca0,0xdcb8);rowText(env_.Strings.at(0xded0),0xdca8,0xdcbc,{0,0,255,255});
          rowText(entry.Text[1],0xdcb0,0xdcb8);rowText(entry.Text[2],0xdcc0,0xdcc8);
        }else rowText(env_.Strings.at(0xdf40),0xdcc0,0xdcc8);
        s=env_.Sprites.at(0xdc04);s.Alignment=0;s.Y+=offset;s.Color=Color(unlocked?0xdc28:0xdc2c);Sprite(43,s);
        const auto size=FontSize(0xdc60);auto p=env_.Points.at(0xdc54);p[1]+=offset-int32_t(size/2);
        Text(Digit(size_t(row+1)/10),p,size,true);p[0]+=Word(0xdc5c);Text(Digit(size_t(row+1)%10),p,size,true);
        if(screen_.Status(size_t(row))==1){const auto& anchor=env_.Sprites.at(0xdcdc);
          for(const auto& marker:BuildNewMarker(marker_,markerEnv_,{ProfileTextureSection::SharedUi,-1,18},{anchor.X,anchor.Y+offset}))shared_.Sprite(marker);
        }
      }
    }
    Sprite(45,env_.Sprites.at(0xdd5c));auto p=env_.Points.at(0xdccc);
    if(screen_.Unlocked(size_t(screen_.Selected()))){
      shared_.Sprite(ResolveUiSprite({ProfileTextureSection::ExtrasBattleThumbnail,-1,uint32_t(screen_.Selected())},env_.Sprites.at(0xdd80)));
      const auto& description=env_.BattleReplays.at(size_t(screen_.Selected())).Text[3];const auto& separator=env_.Strings.at(0xdf08);
      if(separator.empty())throw FormatError("battle description separator missing");
      size_t start=0;for(;;){const auto end=description.find(separator,start);
        Text(std::u16string_view(description).substr(start,end==description.npos?description.size()-start:end-start),p,FontSize(0xdcd8));
        if(end==description.npos)break;start=end+separator.size();p[1]+=Word(0xdcd4);
      }
    }else {Sprite(44,env_.Sprites.at(0xdda4));Text(env_.Strings.at(0xdf78),p,FontSize(0xdcd8));}
    s=env_.Common.SharedSprites[3];s.Alignment=0;shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,7},s));
    const auto size=uint32_t(float(shared_.FontSize)*env_.Common.SharedScale);
    Counter(env_.BattleReplays.size(),env_.Common.SharedPoints[0],size);Counter(screen_.UnlockedCount(),env_.Common.SharedPoints[1],size);
    s=env_.Common.SharedSprites[5];s.Color={255,255,255,255};shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,2},s));
    for(const auto& t:BuildButtonHelp(help_.BattleRows.at(0),help_.BattleRectangle,style_,shared_.Font.Cached(),uint32_t(shared_.FontSize),shared_.Font.NominalSize()))
      Text(t.Text,t.Position,t.Size,false,t.Color);
    shared_.Draw(question_,env_.Strings.at(0xde60),layout_,1,{255,255,255,90});contacts_.Latch(question_,layout_,shared_.Environment);
    Rectangle(uint8_t(screen_.Fade()));GlCheck("Extras battle presentation");
  }
 private:
  int32_t Word(uint16_t key)const{return std::bit_cast<int32_t>(env_.Scalars.at(key));}
  std::array<uint8_t,4> Color(uint16_t key)const{const auto c=env_.Scalars.at(key);return {uint8_t(c>>16),uint8_t(c>>8),uint8_t(c),uint8_t(c>>24)};}
  uint32_t FontSize(uint16_t key)const{const auto size=float(shared_.FontSize)*std::bit_cast<float>(env_.Scalars.at(key));
    if(!std::isfinite(size) || size<0 || size>1024)throw FormatError("invalid battle gallery font size");return uint32_t(size);}
  static std::u16string Digit(size_t n){return std::u16string(1,char16_t(0xff10+n%10));}
  void Text(std::u16string_view text,std::array<int32_t,2> p,uint32_t size,bool center=false,std::array<uint8_t,4> color={0,0,0,255}){
    if(center)p[0]-=int32_t(MeasurePvfText(shared_.Font.Cached(),text,size,shared_.Font.NominalSize())/2);
    shared_.Textures.DrawString(shared_.Renderer,shared_.Font,text,p[0],p[1],size,color);
  }
  void Counter(size_t value,std::array<int32_t,2> p,uint32_t size){for(size_t i=0;i<2;++i){const auto digit=Digit(value);
    const auto width=MeasurePvfText(shared_.Font.Cached(),digit,size,shared_.Font.NominalSize());Text(digit,{p[0]-int32_t(width),p[1]},size);p[0]-=env_.Common.SharedValue;value/=10;}}
  void Sprite(uint32_t id,const UiSpriteDefinition& s){shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SystemUi,-1,id},s));}
  void Stretch(ProfileTextureKey key,const UiSpriteDefinition& s,std::array<int32_t,2> size){auto b=ResolveUiSprite(key,s);
    b.Destination={s.X-(s.Alignment%3==1?size[0]/2:s.Alignment%3==2?size[0]:0),s.Y-(s.Alignment/3==1?size[1]/2:s.Alignment/3==2?size[1]:0),size[0],size[1]};shared_.Sprite(b);}
  void Rectangle(uint8_t alpha){UiSpriteBlit b;b.Destination={0,0,960,544};b.Color={255,255,255,alpha};shared_.Renderer.DrawQuad(b,{0,1,1},true);}
  void RetireTextures(){for(const auto& key:owned_){auto& textures=shared_.Renderer.Textures;const auto it=textures.find(key);
    if(it!=textures.end()){glDeleteTextures(1,&it->second.Handle);textures.erase(it);}}owned_.clear();}
  SceneDialogLayer& shared_;SemanticState& owner_;PlaybackTitleAudio& audio_;MusicPlayback& playback_;ViewerWindow& window_;
  ArchiveBgmResources& music_;ArchiveSeResources& sounds_;std::filesystem::path root_,saveRoot_;NativeSlotData& slots_;
  std::function<void()> restoreMusic_;uint32_t hold_;uint64_t now_=0;
  ExtrasScreenEnvironment env_;ExtrasChildButtonHelpEnvironment help_;ButtonHelpStyle style_;
  NewMarkerEnvironment markerEnv_;NewMarkerState marker_;ExtrasBattleBrowser screen_;ExtrasBattleInput input_;DialogInput modalInput_;
  DialogState question_;DialogContentLayout layout_;PcDialogContacts contacts_;std::vector<int32_t> scripts_;
  std::set<ProfileTextureKey> owned_;bool windowClosed_=false;
};
