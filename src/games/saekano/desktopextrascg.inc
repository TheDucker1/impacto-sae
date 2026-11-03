// CG resources and audio use the same host owners as the scenario and the other
// Extras children. No extracted image tree or second HCA/texture decoder.
class PcExtrasCg final:public ExtrasCgServices {
 public:
  PcExtrasCg(SceneDialogLayer& shared,SemanticState& owner,PlaybackTitleAudio& audio,
      MusicPlayback& playback,const std::filesystem::path& root,
      std::span<const int32_t> repeats,uint32_t hold,bool unlockAll,bool commentHelp,uint64_t now)
      :shared_(shared),owner_(owner),audio_(audio),playback_(playback),
       env_(ExtrasScreenEnvironment::DecodeThroughMovies(ReadGameFile(root, Profile::Saekano::GameAsset::ExtrasEnvironment))),
       childHelp_(ExtrasChildButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment))),
       help_(ExtrasButtonHelpEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::HelpEnvironment))),
       style_(ButtonHelpStyle::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
       markerEnv_(NewMarkerEnvironment::Decode(ReadGameFile(root, Profile::Saekano::GameAsset::GlobalEnvironment))),
       marker_(StartNewMarker(markerEnv_,now)),
       comments_(std::make_shared<AwbBank>(root/"comment.awb")),
       screen_(env_,AlbumGalleryCatalogue(DecodeAlbumEntries(shared.Resources.LoadExtrasCgCatalogue().Data,env_.Common.ImageColumns)),
           owner,*this,repeats,hold,unlockAll,now),commentHelp_(commentHelp){
    for(const auto& [key,texture]:shared.Renderer.Textures){(void)texture;prior_.insert(key);}
    try{
      for(const auto& entry:screen_.Catalogue().Entries){
        const auto id=entry.Gates[1];
        if(id<0 || size_t(id)>=shared.Resources.ExtrasCgThumbnailCount())throw FormatError("CG thumbnail outside authored archive");
        const ProfileTextureKey key{ProfileTextureSection::ExtrasCgThumbnail,-1,uint32_t(id)};
        if(!shared.Renderer.Textures.contains(key))shared.Renderer.Upload(key,DecodeSceneImage(shared.Resources.LoadProfileTexture(key).Data));
        (void)comments_->Index().Find(uint32_t(entry.Gates[0]));
      }
    }catch(...){RetireTextures();throw;}
  }
  ~PcExtrasCg(){StopComment();RetireTextures();}
  void BeginFrame(const ExtrasCgInput& input,const WidgetInputHistory::Snapshot& contacts){input_=input;screen_.BeginFrame(contacts);}
  void CancelPointer(){screen_.CancelPointer();}
  int32_t Update(uint64_t now){const auto result=screen_.Update(input_,now);if(!result)UpdateNewMarker(marker_,markerEnv_,now);return result;}
  bool Dirty()const{return screen_.Dirty();}
  bool ResourcesReady()const override{
    for(const auto& entry:screen_.Catalogue().Entries)
      if(!shared_.Renderer.Textures.contains({ProfileTextureSection::ExtrasCgThumbnail,-1,uint32_t(entry.Gates[1])}))return false;
    return true;
  }
  void PrepareImage(int32_t image,size_t buffer)override{
    const ProfileTextureKey key{ProfileTextureSection::SceneImage,int32_t(ImageSection::Event),uint32_t(image)};
    if(!shared_.Renderer.Textures.contains(key))shared_.Renderer.Upload(key,DecodeSceneImage(shared_.Resources.LoadSceneImage(ImageSection::Event,size_t(image)).Data));
    const auto old=images_.at(buffer);images_[buffer]=key;
    if(old && *old!=key && images_.at(buffer^1)!=old && !prior_.contains(*old)){
      auto& textures=shared_.Renderer.Textures;const auto found=textures.find(*old);
      if(found!=textures.end()){Impacto::Renderer->FreeTexture(found->second.Handle);textures.erase(found);}
    }
  }
  bool ImageReady(size_t buffer)const override{return images_.at(buffer) && shared_.Renderer.Textures.contains(*images_[buffer]);}
  void PrepareComment(int32_t wave)override{
    if(wave<0 || !owner_.VoiceSettings)throw UnsupportedCommand("CG commentary requires known voice settings");
    playback_.PrepareOrdinaryVoice(7,uint32_t(wave),std::make_unique<HcaWaveReader>(comments_,uint32_t(wave)),
        ProfileVoiceGain(owner_.VoiceSettings->Volume,1.0f));commentPresent_=true;
  }
  void StartComment()override{playback_.StartOrdinaryVoice(7);}
  void StopComment()override{playback_.StopOrdinaryVoice(7);commentPresent_=false;}
  bool CommentPlaying()const override{return commentPresent_ && playback_.OrdinaryVoicePlaying(7);}
  bool CommentIdle()const override{return !commentPresent_ || !playback_.OrdinaryVoicePlaying(7);}
  void UiSound(int32_t id)override{audio_.PlayUi(id);}
  const ExtrasCgBrowser& State()const{return screen_;}
  void Draw(){
    ScopedSceneRenderState scope;auto& r=shared_.Renderer;
    r.Bind();

    r.SetAlpha(0);r.SetFilter(0,0);ApplyParticleBlend(1);
    const auto rectangle=[&](uint8_t alpha){UiSpriteBlit b;b.Destination={0,0,960,544};b.Color={255,255,255,alpha};r.DrawQuad(b,{0,1,1},true);};
    if(screen_.Phase()<=1)return;
    rectangle(128);
    auto fill=ResolveUiSprite({ProfileTextureSection::TitleUi,-1,58},env_.Common.SharedSprites[1]);
    fill.Destination[2]=env_.Common.StretchSize[0];fill.Destination[3]=env_.Common.StretchSize[1];shared_.Sprite(fill);
    Sprite(58,env_.Common.SharedSprites[0]);Sprite(14,env_.Sprites.at(0xd24c));
    const auto& list=screen_.List();
    for(size_t i=0;i<screen_.Catalogue().PageCardCounts.size();++i){
      if(int32_t(i)==list.Rows.Selected){auto s=env_.Sprites.at(0xd2d0);const auto& base=env_.Sprites.at(0xd24c);
        s.X+=base.X+int32_t(i)*Word(0xd2f4);s.Y+=base.Y;Sprite(15,s);}
      const auto size=FontSize(0xd2cc);const auto& p=env_.Points.at(0xd2b8);const auto& step=env_.Points.at(0xd2c0);
      const int32_t x=p[0]+int32_t(i)*step[0],y=p[1]-int32_t(size/2);
      Text(Number((i+1)/10),{x,y},size,{0,0,0,255},true);
      Text(Number((i+1)%10),{x+Word(0xd2c8),y},size,{0,0,0,255},true);
    }
    Sprite(1,env_.Sprites.at(0xd270));auto arrow=ResolveUiSprite({ProfileTextureSection::TitleUi,-1,1},env_.Sprites.at(0xd294));
    arrow.Source[2]=-arrow.Source[2];shared_.Sprite(arrow);
    static constexpr std::array<uint32_t,6> labels{4,13,8,6,7,11};
    const auto& tabs=env_.SpriteTables.at(0xd228);
    for(size_t i=0;i<tabs.size() && i<labels.size();++i){auto s=tabs[i];
      if(int32_t(i)==screen_.Category())s.X+=Word(0xd238);else s.Color={128,128,128,255};
      Sprite(labels[i],s);if(int32_t(i)==screen_.Category())Sprite(3,s);
    }
    SlotViewportInputs viewport;viewport.Visible=true;viewport.Animated=true;viewport.Scrolling=list.Scrolling;viewport.Rows=list.Rows;
    viewport.Rectangle=env_.Rectangles.at(0xd2f8);viewport.Pitch=list.Settings.Pitch;
    viewport.ScrollOffset=list.Scroll.Current;viewport.CursorOffset=list.Cursor.Current;
    for(const auto& command:BuildSlotViewport(viewport)){
      if(command.Kind!=SlotViewportKind::Cards){r.Execute(std::span<const UiRenderCommand>(&command.Render,1));continue;}
      const auto& cards=command.Cards;
      for(int32_t page=cards.Begin;page<cards.End;++page){
        if(page<0 || size_t(page)>=screen_.Catalogue().PageCardCounts.size())continue;
        const auto& origin=env_.Points.at(0xd314);const auto& pitch=env_.Points.at(0xd340);
        for(int32_t cell=0;cell<9;++cell){const size_t index=size_t(page*9+cell);const auto* entry=screen_.Catalogue().Card(index);if(!entry)continue;
          const int32_t x=origin[0]+(page-cards.FirstVisible)*list.Settings.Pitch+cards.Offsets[0]+cell%3*pitch[0];
          const int32_t y=origin[1]+cards.Offsets[1]+cell/3*pitch[1];
          auto sprite=[&](uint32_t id,uint16_t field){auto s=env_.Sprites.at(field);s.X+=x;s.Y+=y;Sprite(id,s);};
          sprite(12,0xd31c);const auto variants=screen_.Variants(index);const auto count=size_t(std::count(variants.begin(),variants.end(),true));
          if(count){auto s=env_.Sprites.at(0xd348);s.X+=x;s.Y+=y;
            shared_.Sprite(ResolveUiSprite({ProfileTextureSection::ExtrasCgThumbnail,-1,uint32_t(entry->Gates[1])},s));
          }else sprite(9,0xd374);
          sprite(10,0xd398);
          const auto& face=env_.Sprites.at(0xd398);
          if(variants.size()>1){const auto size=FontSize(0xd40c);
            const auto label=[&](std::u16string_view value,uint16_t at){const auto& p=env_.Points.at(at);
              Text(value,{x+face.X+p[0],y+face.Y+p[1]-int32_t(size)},size,{255,255,255,255});};
            label(Number(count),0xd3bc);label(Number(variants.size()),0xd3c4);label(env_.Strings.at(0xd3cc),0xd404);
          }
          const auto size=FontSize(0xd418);const auto& p=env_.Points.at(0xd410);
          const auto number=Number(*screen_.Catalogue().Cards[index]+1,3);
          const auto width=MeasurePvfText(shared_.Font.Cached(),number,size,shared_.Font.NominalSize());
          Text(number,{x+face.X+p[0]-int32_t(width),y+face.Y+p[1]-int32_t(size)},size,{255,255,255,255});
          if(screen_.Phase()>=5 && screen_.Phase()<=10 && cell==screen_.Cell())sprite(5,0xd41c);
          if(screen_.New(index)){const auto& anchor=env_.Points.at(0xd36c);
            for(const auto& b:BuildNewMarker(marker_,markerEnv_,{ProfileTextureSection::SharedUi,-1,18},{x+anchor[0],y+anchor[1]}))shared_.Sprite(b);}
        }
      }
    }
    const auto image=[&](int32_t buffer,int32_t alpha){if(buffer<0)return;const auto key=images_.at(size_t(buffer));if(!key)return;
      UiSpriteBlit b;b.Texture=*key;b.Destination={0,0,960,544};
      b.Source={0,0,960,544};b.Color={255,255,255,uint8_t(alpha)};shared_.Sprite(b);};
    image(screen_.CurrentBuffer(),screen_.ImageOpacity());image(screen_.PreviousBuffer(),screen_.PreviousOpacity());
    if(screen_.Viewing()){
      if(screen_.HelpVisible()){
        auto s=env_.Common.SharedSprites[3];s.Alignment=0;shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,6},s));
        Help(!commentHelp_?1:screen_.CommentActive()?3:2,help_.Rectangle);
        const auto variants=screen_.Variants(size_t(screen_.SelectedCard()));
        for(size_t i=0;i<variants.size();++i){auto dot=env_.Sprites.at(0xd47c);dot.X+=int32_t(i)*Word(0xd4a0);
          Sprite(!variants[i]?16:int32_t(i)==screen_.Variant()?18:17,dot);}
      }
    }else {
      auto s=env_.Common.SharedSprites[4];s.Alignment=0;shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,7},s));
      Help(0,childHelp_.AlbumRectangles[0]);
      const auto size=uint32_t(float(shared_.FontSize)*env_.Common.SharedScale);
      Counter(screen_.UnlockedImages(),env_.Common.SharedPoints[1],size);
      Counter(screen_.TotalImages(),env_.Common.SharedPoints[0],size);
      auto back=env_.Common.SharedSprites[5];back.Color={255,255,255,255};shared_.Sprite(ResolveUiSprite({ProfileTextureSection::SharedUi,-1,2},back));
    }
    rectangle(uint8_t(screen_.Fade()));GlCheck("Extras CG presentation");
  }
 private:
  int32_t Word(uint16_t key)const{return std::bit_cast<int32_t>(env_.Scalars.at(key));}
  uint32_t FontSize(uint16_t key)const{const float size=float(shared_.FontSize)*std::bit_cast<float>(env_.Scalars.at(key));
    if(!std::isfinite(size) || size<0 || size>1024)throw FormatError("invalid CG font size");return uint32_t(size);}
  static std::u16string Number(size_t value,size_t width=0){auto ascii=std::to_string(value);if(ascii.size()<width)ascii.insert(0,width-ascii.size(),'0');
    std::u16string result;for(const auto c:ascii)result.push_back(char16_t(0xff10+c-'0'));return result;}
  void Text(std::u16string_view text,std::array<int32_t,2> p,uint32_t size,std::array<uint8_t,4> color,bool center=false){
    if(center)p[0]-=int32_t(MeasurePvfText(shared_.Font.Cached(),text,size,shared_.Font.NominalSize())/2);
    shared_.Textures.DrawString(shared_.Renderer,shared_.Font,text,p[0],p[1],size,color);
  }
  void Sprite(uint32_t id,const UiSpriteDefinition& sprite){shared_.Sprite(ResolveUiSprite({ProfileTextureSection::TitleUi,-1,id},sprite));}
  void Counter(size_t value,std::array<int32_t,2> p,uint32_t size){
    // Native19a7fc, alignment2: two least-significant digits, each right-aligned
    // at the authored pitch. This is not proportional whole-string centering.
    for(size_t i=0;i<2;++i){const auto digit=Number(value%10);const auto width=MeasurePvfText(shared_.Font.Cached(),digit,size,shared_.Font.NominalSize());
      Text(digit,{p[0]-int32_t(width),p[1]},size,{0,0,0,255});p[0]-=env_.Common.SharedValue;value/=10;}
  }
  void Help(size_t row,std::array<int32_t,4> rectangle){
    for(const auto& t:BuildButtonHelp(childHelp_.AlbumRows[0].at(row),rectangle,style_,shared_.Font.Cached(),uint32_t(shared_.FontSize),shared_.Font.NominalSize()))
      Text(t.Text,t.Position,t.Size,t.Color);
  }
  void RetireTextures()noexcept{
    auto& textures=shared_.Renderer.Textures;
    for(auto it=textures.begin();it!=textures.end();){if(!prior_.contains(it->first)){Impacto::Renderer->FreeTexture(it->second.Handle);it=textures.erase(it);}else ++it;}
  }
  SceneDialogLayer& shared_;SemanticState& owner_;PlaybackTitleAudio& audio_;MusicPlayback& playback_;
  ExtrasScreenEnvironment env_;ExtrasChildButtonHelpEnvironment childHelp_;ExtrasButtonHelpEnvironment help_;ButtonHelpStyle style_;
  NewMarkerEnvironment markerEnv_;NewMarkerState marker_;
  std::shared_ptr<AwbBank> comments_;std::array<std::optional<ProfileTextureKey>,2> images_;
  ExtrasCgBrowser screen_;ExtrasCgInput input_;std::set<ProfileTextureKey> prior_;bool commentPresent_=false,commentHelp_;
};
