#include "extras.h"
#include "commands.h"
#include <cmath>

namespace Impacto::Saekano {
namespace {
UiSpriteDefinition Contact(std::array<int32_t,4> r) {
  UiSpriteDefinition s;s.X=r[0];s.Y=r[1];s.Width=r[2];s.Height=r[3];return s;
}
UiSpriteDefinition White(UiSpriteDefinition s){s.Color={255,255,255,255};return s;}
}
ExtrasRoot::ExtrasRoot(AlbumEnvironment environment,std::span<const int32_t> repeats,
    uint32_t hold,MusicHistoryState& history,ExtrasServices& services,int32_t initialSection)
    :env_(std::move(environment)),services_(services),history_(history),
     cancel_(initialSection==-1?White(env_.SharedSprites[5]):UiSpriteDefinition{},
             {ProfileTextureSection::SharedUi,-1,2},1,true,hold),
     section_(initialSection) {
  if(initialSection < -1 || initialSection>3)throw FormatError("Extras section outside four native children");
  if(env_.FadeDuration<0)throw FormatError("negative Extras fade duration");
  if(initialSection==-1) {
    if(env_.ButtonRectangles.size()<4)throw FormatError("Extras compound button regions are missing");
    for(size_t i=0;i<4;++i) {
      const auto& sprite=env_.Buttons[i];
      // Native rectangle faces use raw X/Y/width/height, independent of the
      // aligned sprite that the draw routine submits later.
      buttons_.emplace_back(Contact({sprite.X,sprite.Y,sprite.Width,sprite.Height}),
          ProfileTextureKey{ProfileTextureSection::SystemUi,-1,0},1.f,true,hold);
      for(const auto& rect:env_.ButtonRectangles[i])
        regions_[i].emplace_back(Contact(rect),ProfileTextureKey{ProfileTextureSection::SystemUi,-1,0},1.f,true,hold);
    }
  }
  StartOptionsNavigation(navigation_,rows_,4,4,1,repeats);
}
void ExtrasRoot::CancelPointer() {
  contacts_.Current=contacts_.Previous={};
  const auto clear=[](BattleCardSprite& w){w.Input.Flags=0;w.Input.TrackedId=-1;};
  clear(cancel_);for(auto& w:buttons_)clear(w);
  for(auto& group:regions_)for(auto& w:group)clear(w);
}
void ExtrasRoot::Sample(BattleCardSprite& widget,std::array<int32_t,2> offset,uint64_t now) {
  widget.Update(offset,SampleWidgetContact(widget.HitRectangle(offset),contacts_.Current[0],contacts_.Previous[0],
      widget.Input.TrackedId,contacts_.Velocity(0)),now);
  contacts_.Publish(widget.Input.Flags);
}
bool ExtrasRoot::Select(const ExtrasInput& input,uint64_t now) {
  // Accept has priority over navigation, which has priority over pointer rows.
  if(input.Accept)return true;
  UpdateOptionsNavigation(navigation_,rows_,input.Vertical);
  for(uint32_t i=0;i<navigation_.Sounds;++i)services_.UiSound(2);
  if(navigation_.Any)return false;
  if(buttons_.empty())return false;
  bool released=false;
  for(size_t i=0;i<4;++i) {
    Sample(buttons_.at(i),{Selected()==int32_t(i)?env_.SelectedOffset:0,0},now);
    bool rowRelease=buttons_[i].Input.Flags&8;
    for(auto& widget:regions_[i]){Sample(widget,{0,0},now);rowRelease|=bool(widget.Input.Flags&8);}
    if(rowRelease){SelectOptionsRow(rows_,int32_t(i));released=true;}
  }
  return released;
}
int32_t ExtrasRoot::Update(const ExtrasInput& input,uint64_t now) {
  if(closed_)return 2;
  if(pending_){phase_=*pending_;pending_.reset();}
  if(phase_==0) {
    phase_=section_==-1?1:3;
    services_.SetExtrasAudio(true);
    // Native4623a..46532: preserve played states; fallback-filled growth then
    // set only music1. Partial native histories remain strict through the owner.
    MarkMusicPlayed(history_,1);
  }
  if(phase_==1){fade_.Configure(255,0,uint32_t(env_.FadeDuration),true,now);phase_=2;}
  if(phase_==2) {
    fade_.Tick(now);
    if(fade_.Finished()) {
      Sample(cancel_,{0,0},now);
      if(input.Cancel || (cancel_.Input.Flags&8)){services_.UiSound(4);pending_=9;}
      else if(Select(input,now)) {
        section_=Selected();services_.UiSound(3);pending_=3;fade_.Reverse(now);
      }
    }
  }
  if(phase_==3) {
    fade_.Tick(now);
    if(fade_.Finished()) {
      services_.BeginChild(ExtrasSection(section_));child_=true;phase_=4;
    }
  }
  if(phase_==4 && services_.UpdateChild()==1)pending_=5;
  if(phase_==5) {
    pending_=services_.ChildDirty()?6:1;
    services_.DestroyChild();child_=false;
  }
  if(phase_==6){services_.BeginSaveQuestion(env_.SavePrompt);pending_=7;}
  if(phase_==7) {
    const auto result=services_.UpdateSaveQuestion();
    if(!result.Busy) {
      if(!result.Cancelled && result.Selection==1){services_.BeginSystemSave();pending_=8;}
      else pending_=1;
    }
  }
  if(phase_==8) {
    const auto result=services_.UpdateSystemSave();
    if(!result.Busy){phase_=result.NativeResult?1:6;}
  }
  if(phase_==9){fade_.Reverse(now);phase_=10;}
  if(phase_==10) {
    fade_.Tick(now);
    if(fade_.Finished()){services_.SetExtrasAudio(false);closed_=true;return 2;}
  }
  return 0;
}
std::vector<ExtrasDrawCommand> BuildExtrasRootDraw(const ExtrasRoot& root,uint32_t utilitySize,
    const std::function<int32_t(std::u16string_view,uint32_t)>& measure) {
  const auto& e=root.Environment();const auto selected=root.Selected();
  if(selected<0 || selected>3)throw FormatError("Extras selection outside four draw rows");
  std::vector<ExtrasDrawCommand> out;
  const auto operation=[&](ExtrasDrawKind kind){ExtrasDrawCommand c;c.Kind=kind;out.push_back(std::move(c));};
  const auto sprite=[&](ProfileTextureSection section,uint32_t id,const UiSpriteDefinition& s) {
    ExtrasDrawCommand c;c.Sprite=ResolveUiSprite({section,-1,id},s);out.push_back(std::move(c));
  };
  if(root.Phase()!=4) {
    operation(ExtrasDrawKind::BeginMask);
    sprite(ProfileTextureSection::SystemUi,46,e.Backdrop);
    operation(ExtrasDrawKind::Multiply);
    constexpr std::array<uint32_t,4> headers{47,48,49,55};
    sprite(ProfileTextureSection::SystemUi,headers[size_t(selected)],e.SectionHeaders[size_t(selected)]);
    operation(ExtrasDrawKind::EndMask);
  }
  sprite(ProfileTextureSection::SystemUi,46,e.Backdrop);
  // The target uses the same authored geometry/UV/tint as the backdrop.
  ExtrasDrawCommand mask;mask.Kind=ExtrasDrawKind::Mask;
  mask.Sprite=ResolveUiSprite({},e.Backdrop);out.push_back(std::move(mask));
  if(root.ChildPresent())operation(ExtrasDrawKind::Child);
  else {
    auto stretch=e.SharedSprites[1];stretch.Width=e.StretchSize[0];stretch.Height=e.StretchSize[1];
    auto fill=ResolveUiSprite({ProfileTextureSection::SystemUi,-1,59},stretch);
    fill.Source[2]=e.SharedSprites[1].Width;fill.Source[3]=e.SharedSprites[1].Height;
    ExtrasDrawCommand c;c.Sprite=fill;out.push_back(std::move(c));
    sprite(ProfileTextureSection::SystemUi,59,e.SharedSprites[0]);
    auto shared=e.SharedSprites[2];shared.Alignment=0;
    sprite(ProfileTextureSection::SharedUi,8,shared);
    shared=e.SharedSprites[3];shared.Alignment=0;
    sprite(ProfileTextureSection::SharedUi,6,shared);
    operation(ExtrasDrawKind::Help);
    if(root.ControlsConfigured())sprite(ProfileTextureSection::SharedUi,2,White(e.SharedSprites[5]));
    constexpr std::array<uint32_t,4> textures{50,52,53,54};
    for(size_t i=0;i<4;++i) {
      auto button=e.Buttons[i];const bool current=int32_t(i)==selected;
      button.Color=current?std::array<uint8_t,4>{255,255,255,255}:std::array<uint8_t,4>{128,128,128,255};
      if(current)button.X=std::bit_cast<int32_t>(uint32_t(button.X)+uint32_t(e.SelectedOffset));
      sprite(ProfileTextureSection::SystemUi,textures[i],button);
      if(current) {
        auto marker=e.SelectedMarker;marker.Alignment=0;
        marker.Y=std::bit_cast<int32_t>(uint32_t(e.Buttons[i].Y)+uint32_t(marker.Y));
        sprite(ProfileTextureSection::SystemUi,51,marker);
        const float scaled=float(utilitySize)*e.MenuTextScale;
        if(!std::isfinite(scaled) || scaled<0 || scaled>1024 || !measure)
          throw FormatError("Extras description font configuration is unavailable");
        ExtrasDrawCommand text;text.Kind=ExtrasDrawKind::Text;
        const auto& label=e.MenuLabels.at(i);const auto size=uint32_t(scaled);
        text.Text={label,{std::bit_cast<int32_t>(uint32_t(e.MenuTextPosition[0])-uint32_t(measure(label,size))),
            e.MenuTextPosition[1]},size,{0,0,0,255}};
        out.push_back(std::move(text));
      }
    }
    ExtrasDrawCommand fade;fade.Kind=ExtrasDrawKind::Fade;
    fade.Sprite.Destination={0,0,960,544};fade.Sprite.Color={0,0,0,uint8_t(root.Fade())};out.push_back(std::move(fade));
  }
  operation(ExtrasDrawKind::Overlays);
  return out;
}

}
