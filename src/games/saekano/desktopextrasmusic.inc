class PcExtrasMusic final:public ExtrasMusicServices {
 public:
  PcExtrasMusic(SceneDialogLayer& shared,SemanticState& owner,PlaybackTitleAudio& audio,
      MusicPlayback& playback,const std::filesystem::path& root,std::function<void()> restoreMusic,
      std::span<const int32_t> repeats,uint32_t hold,bool unlockAll,uint64_t now)
      :shared_(shared),audio_(audio),playback_(playback),restoreMusic_(std::move(restoreMusic)),
       env_(ExtrasScreenEnvironment::DecodeThroughMovies(ReadGameFile(root, Profile::Saekano::GameAsset::ExtrasEnvironment))),
       childHelp_(ExtrasChildButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment))),
       style_(ButtonHelpStyle::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
       markerEnv_(NewMarkerEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
       marker_(StartNewMarker(markerEnv_,now)),bank_(std::make_shared<AwbBank>(root/"music.awb")),
       screen_(env_,owner,*this,repeats,hold,unlockAll,now){
    if(!restoreMusic_ || childHelp_.AlbumRows[1].size()<6)throw FormatError("music child help/audio owner missing");
    for(const auto& t:env_.MusicTracks)(void)bank_->Index().Find(uint32_t(t.Wave));
  }
  ~PcExtrasMusic(){if(!returned_)playback_.StopBgmImmediately();}
  void BeginFrame(const ExtrasMusicInput& input,const WidgetInputHistory::Snapshot& contacts){input_=input;screen_.BeginFrame(contacts);}
  void CancelPointer(){screen_.CancelPointer();}
  int32_t Update(uint64_t now){const auto result=screen_.Update(input_,now);if(!result)UpdateNewMarker(marker_,markerEnv_,now);return result;}
  bool Dirty()const{return screen_.Dirty();}
  const ExtrasMusicBrowser& State()const{return screen_;}
  void PlayMusic(int32_t wave)override{playback_.PlayGalleryMusic(wave,std::make_unique<HcaWaveReader>(bank_,uint32_t(wave)));}
  void StopMusic()override{playback_.StopBgmImmediately();}
  void SetPaused(bool paused)override{playback_.SetBgmPaused(paused);}
  bool Paused()const override{return playback_.BgmPaused();}
  bool Busy()const override{return playback_.BgmBusy();}
  void RestoreExtrasMusic()override{restoreMusic_();returned_=true;}
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  void Draw(){
    ScopedSceneRenderState scope;auto& r=shared_.Renderer;
    r.Bind();

    r.SetAlpha(0);r.SetFilter(0,0);ApplyParticleBlend(1);
    const auto rectangle=[&](uint8_t alpha){UiSpriteBlit b;b.Destination={0,0,960,544};b.Color={255,255,255,alpha};r.DrawQuad(b,{0,1,1},true);};
    rectangle(128);
    auto s=env_.Sprites.at(0xd4a4);s.Color[3]=uint8_t(Word(0xd4c8));Sprite(19,s);
    s=env_.Sprites.at(0xd4cc);s.Color[3]=uint8_t(Word(0xd4c8));Sprite(23,s);
    Stretch({ProfileTextureSection::TitleUi,-1,61},env_.Common.SharedSprites[1],env_.Common.StretchSize);
    Sprite(61,env_.Common.SharedSprites[0]);Sprite(26,env_.Sprites.at(0xd4f0));
    const auto playing=screen_.PlayingRow();
    Text(playing<0?env_.Strings.at(0xd80c):env_.MusicTracks.at(size_t(playing)).Title,
        env_.Points.at(0xd76c),FontSize(0xd774),true);
    const auto position=playback_.BgmPositionMs();
    const auto stopped=uint64_t(std::max(0,Word(0xd844)));
    const auto duration=playing<0?stopped:uint64_t(env_.MusicTracks.at(size_t(playing)).DurationSeconds);
    Text(Time(playing<0?stopped:position/1000,playing<0),env_.Points.at(0xd778),FontSize(0xd788));
    Text(Time(duration,playing<0),env_.Points.at(0xd780),FontSize(0xd788));
    if(playing>=0){
      const auto& definition=env_.Sprites.at(0xd514);
      auto fill=ResolveUiSprite({ProfileTextureSection::TitleUi,-1,27},definition);
      fill.Destination={definition.X,definition.Y,int32_t(float(Word(0xd538))*(float(position)/float(duration*1000))),definition.Height};
      fill.Color={255,255,255,255};shared_.Sprite(fill);
    }
    const auto& list=screen_.List();SlotViewportInputs viewport;
    viewport.Visible=true;viewport.Animated=true;viewport.Scrolling=list.Scrolling;viewport.Rows=list.Rows;
    viewport.Rectangle=env_.Rectangles.at(0xd688);viewport.Pitch=list.Settings.Pitch;
    viewport.ScrollOffset=list.Scroll.Current;viewport.CursorOffset=list.Cursor.Current;
    for(const auto& command:BuildSlotViewport(viewport)){
      if(command.Kind!=SlotViewportKind::Cards){r.Execute(std::span<const UiRenderCommand>(&command.Render,1));continue;}
      const auto& cards=command.Cards;
      s=env_.Sprites.at(0xd6f8);s.Y+=Word(0xd6a4)+list.Settings.Pitch*(cards.Selected-cards.FirstVisible)+cards.Offsets[1];
      s.Alignment=0;Sprite(22,s);
      for(int32_t i=cards.Begin;i<cards.End;++i){
        if(i<0 || size_t(i)>=env_.MusicTracks.size())continue;
        const auto offset=Word(0xd6a4)+(i-cards.FirstVisible)*list.Settings.Pitch+cards.Offsets[0];
        s=env_.Sprites.at(0xd6d4);s.Alignment=0;s.Y+=offset;Sprite(24,s);
        s=env_.Sprites.at(0xd6a8);s.Alignment=0;s.Y+=offset;
        const auto color=env_.Scalars.at(screen_.Unlocked(size_t(i))?0xd6cc:0xd6d0);
        s.Color={uint8_t(color>>16),uint8_t(color>>8),uint8_t(color),uint8_t(color>>24)};Sprite(25,s);
        const auto size=FontSize(0xd738);auto p=env_.Points.at(0xd72c);p[1]+=offset-int32_t(size/2);
        Text(Digit(size_t(i+1)/10),p,size,true);p[0]+=Word(0xd734);Text(Digit(size_t(i+1)%10),p,size,true);
        if(screen_.Status(size_t(i))==1){
          const auto& anchor=env_.Sprites.at(0xd748);
          for(const auto& marker:BuildNewMarker(marker_,markerEnv_,{ProfileTextureSection::SharedUi,-1,18},{anchor.X,anchor.Y+offset}))shared_.Sprite(marker);
        }
        p=env_.Points.at(0xd73c);p[1]+=offset;
        Text(screen_.Unlocked(size_t(i))?env_.MusicTracks[size_t(i)].Title:env_.Strings.at(0xd7d4),p,FontSize(0xd744));
      }
    }
    const auto shared10=ProfileTextureKey{ProfileTextureSection::SharedUi,-1,10};
    shared_.Sprite(ResolveUiSprite(shared10,env_.Sprites.at(0xd53c)));
    const auto extent=env_.Points.at(0xd584);Stretch(shared10,env_.Sprites.at(0xd560),extent);
    shared_.Sprite(ResolveUiSprite(shared10,env_.Sprites.at(0xd58c)));
    float pitch=float(extent[1])/float(env_.MusicTracks.size());int32_t thumb;
    if(pitch<12){thumb=int32_t(int64_t(extent[1]-12)*screen_.Selected()/int64_t(env_.MusicTracks.size()-1));pitch=12;}
    else thumb=int32_t(pitch*float(screen_.Selected()));
    thumb+=env_.Sprites.at(0xd560).Y;
    const auto width=env_.Points.at(0xd61c)[0];const auto& top=env_.Sprites.at(0xd5b0);const auto& bottom=env_.Sprites.at(0xd5f8);
    const auto shared11=ProfileTextureKey{ProfileTextureSection::SharedUi,-1,11};
    s=top;s.Y+=thumb;Stretch(shared11,s,{width,top.Height});
    const auto middle=int32_t(pitch-float(top.Height)-float(bottom.Height));
    s=env_.Sprites.at(0xd5d4);s.Y+=thumb;Stretch(shared11,s,{width,middle});
    s=bottom;s.Y+=thumb+env_.Sprites.at(0xd5d4).Y+middle;Stretch(shared11,s,{width,top.Height});
    Sprite(30,env_.Sprites.at(0xd624));
    Sprite(screen_.PlayIconActive()?31:32,env_.SpriteTables.at(0xd648).at(screen_.PlayIconActive()?1:0));
    Sprite(37,env_.SpriteTables.at(0xd658).at(0));
    constexpr std::array<uint32_t,3> modeIds{36,34,28};Sprite(modeIds.at(size_t(screen_.Mode())),env_.SpriteTables.at(0xd668).at(0));
    s=env_.Common.SharedSprites[3];s.Alignment=0;shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,6},s));
    const auto counterSize=uint32_t(float(shared_.FontSize)*env_.Common.SharedScale);
    Counter(env_.MusicTracks.size(),env_.Common.SharedPoints[0],counterSize);
    Counter(screen_.UnlockedCount(),env_.Common.SharedPoints[1],counterSize);
    s=env_.Common.SharedSprites[5];s.Color={255,255,255,255};shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,2},s));
    const auto helpRow=size_t((screen_.Mode()==2?2:screen_.Mode()==1?4:0)+(Busy()?1:0));
    for(const auto& t:BuildButtonHelp(childHelp_.AlbumRows[1].at(helpRow),childHelp_.AlbumRectangles[1],style_,shared_.Font.Cached(),uint32_t(shared_.FontSize),shared_.Font.NominalSize()))
      Text(t.Text,t.Position,t.Size,false,t.Color);
    rectangle(uint8_t(screen_.Fade()));GlCheck("Extras music presentation");
  }
 private:
  int32_t Word(uint16_t key)const{return std::bit_cast<int32_t>(env_.Scalars.at(key));}
  uint32_t FontSize(uint16_t key)const{const float size=float(shared_.FontSize)*std::bit_cast<float>(env_.Scalars.at(key));
    if(!std::isfinite(size) || size<0 || size>1024)throw FormatError("invalid music gallery font size");return uint32_t(size);}
  static std::u16string Digit(size_t value){return std::u16string(1,char16_t(0xff10+value%10));}
  std::u16string Time(uint64_t seconds,bool stopped)const{
    // Original env_omake uses "%d：%02d", then the existing character map.
    const auto minutes=std::to_string(stopped?seconds:seconds/60);
    auto remainder=std::to_string(stopped?seconds:seconds%60);if(remainder.size()<2)remainder.insert(0,1,'0');
    const auto& format=env_.Strings.at(0xd848);const auto first=format.find(u"%d"),second=format.find(u"%02d");
    if(first==format.npos || second==format.npos || first>=second)throw FormatError("unsupported authored music time format");
    std::u16string text=format.substr(0,first);for(const auto c:minutes)text.push_back(char16_t(c));
    text+=format.substr(first+2,second-first-2);for(const auto c:remainder)text.push_back(char16_t(c));text+=format.substr(second+4);
    VoiceTextReveal converted;converted.PrepareUtf16(text,shared_.Format,0);return converted.Text;
  }
  void Text(std::u16string_view text,std::array<int32_t,2> p,uint32_t size,bool center=false,std::array<uint8_t,4> color={0,0,0,255}){
    if(center)p[0]-=int32_t(MeasurePvfText(shared_.Font.Cached(),text,size,shared_.Font.NominalSize())/2);
    shared_.Textures.DrawString(shared_.Renderer,shared_.Font,text,p[0],p[1],size,color);
  }
  void Counter(size_t value,std::array<int32_t,2> p,uint32_t size){for(size_t i=0;i<2;++i){
    const auto digit=Digit(value);const auto width=MeasurePvfText(shared_.Font.Cached(),digit,size,shared_.Font.NominalSize());
    Text(digit,{p[0]-int32_t(width),p[1]},size);p[0]-=env_.Common.SharedValue;value/=10;}}
  void Sprite(uint32_t id,const UiSpriteDefinition& s){shared_.Sprite(ResolveUiSprite({ProfileTextureSection::TitleUi,-1,id},s));}
  void Stretch(ProfileTextureKey key,const UiSpriteDefinition& s,std::array<int32_t,2> size){
    auto b=ResolveUiSprite(key,s);b.Destination={s.X-(s.Alignment%3==1?size[0]/2:s.Alignment%3==2?size[0]:0),
        s.Y-(s.Alignment/3==1?size[1]/2:s.Alignment/3==2?size[1]:0),size[0],size[1]};shared_.Sprite(b);
  }
  SceneDialogLayer& shared_;PlaybackTitleAudio& audio_;MusicPlayback& playback_;std::function<void()> restoreMusic_;
  ExtrasScreenEnvironment env_;ExtrasChildButtonHelpEnvironment childHelp_;ButtonHelpStyle style_;
  NewMarkerEnvironment markerEnv_;NewMarkerState marker_;std::shared_ptr<AwbBank> bank_;
  ExtrasMusicBrowser screen_;ExtrasMusicInput input_;bool returned_=false;
};
