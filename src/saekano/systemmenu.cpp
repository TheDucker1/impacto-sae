#include "systemmenu.h"
#include "commands.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace Impacto::Saekano {
namespace {
int32_t Add(int32_t a, int32_t b) {
  return std::bit_cast<int32_t>(uint32_t(a) + uint32_t(b));
}
int32_t Negate(int32_t a) { return std::bit_cast<int32_t>(0u - uint32_t(a)); }
UiSpriteDefinition White(UiSpriteDefinition s) { s.Color={255,255,255,255}; return s; }
UiSpriteDefinition BackContact(const SystemMenuEnvironment& e) {
  UiSpriteDefinition s;
  s.X=std::bit_cast<int32_t>(e.Scalars.at(0x19c));
  s.Y=std::bit_cast<int32_t>(e.Scalars.at(0x1a0));
  s.Width=std::bit_cast<int32_t>(e.Scalars.at(0x1a4));
  s.Height=std::bit_cast<int32_t>(e.Scalars.at(0x1a8));
  s.Color={0,0,0,0}; return s;
}
}
int32_t SystemMenu::Word(uint16_t at) const {
  return std::bit_cast<int32_t>(env_.Scalars.at(at));
}
SystemMenu::SystemMenu(SystemMenuEnvironment e, ProfileTextureSection bank,
    uint32_t hold, SystemMenuServices& services)
    : env_(std::move(e)), bank_(bank), services_(services),
      close_(White(env_.Sprites.at(0x154)),Texture(3),1,false,hold),
      back_(White(env_.Sprites.at(0x178)),Texture(3),1,false,hold),
      backContact_(BackContact(env_),Texture(0),1,true,hold) {
  // Original loops address five rows, including parallel labels/effect tables.
  for(uint16_t at=0xc0;at<=0xf0;at+=16)
    if(env_.SpriteTables.at(at).size()!=5)throw FormatError("system menu needs five parallel rows");
  if(env_.IntegerTables.at(0x10c).size()!=5)
    throw FormatError("system menu needs five entry delays");
  for(size_t i=0;i<5;++i) {
    const auto s=White(env_.SpriteTables.at(0xc0)[i]);
    rows_.push_back({{BattleCardSprite(s,Texture(uint32_t(7+i)),1,true,hold),
                      BattleCardSprite(s,Texture(uint32_t(12+i)),1,true,hold)}});
  }
  Highlight(0,true);
}
void SystemMenu::BeginFrame(WidgetInputHistory::Snapshot snapshot) {
  contacts_.BeginFrame(std::move(snapshot));
}
void SystemMenu::CancelPointer() {
  contacts_.Current=contacts_.Previous={};
  for(auto& row:rows_)for(auto& v:row.Variants){v.Input.Flags=0;v.Input.TrackedId=-1;}
  for(auto* v:{&close_,&back_,&backContact_}){v->Input.Flags=0;v->Input.TrackedId=-1;}
}
void SystemMenu::Highlight(int32_t row,bool enabled) {
  // Native group0 stores normal then alternate; condition1 selects variant0.
  rows_.at(size_t(row)).Variant=enabled?0:1;
}
void SystemMenu::Setup(uint64_t now) {
  const auto configure=[&](TitleTween& t,int32_t from,int32_t to,uint16_t duration) {
    t.Configure(from,to,uint32_t(Word(duration)),false,now);
  };
  configure(headerY_,Word(0x84),0,0x88);
  configure(headerOpacity_,0,255,0x80);
  configure(lineOpacity_,0,255,0xb8);
  for(auto& row:rows_) {
    configure(row.Opacity,0,255,0x108);
    configure(row.X,Word(0x100),0,0x104);
  }
  // Static initializer600aa..600e2 installs the same four values at620e0
  // as the already implemented battle card channel's authored motion curve.
  selectionScale_.Configure(std::bit_cast<float>(env_.Scalars.at(0x124)),1,
                            Word(0x128),BattleSelectionMotionCurve(),false,now);
  configure(selectionOpacity_,0,255,0x108);
  configure(selectionWidth_,0,env_.SpriteTables.at(0xe0)[0].Width,0x11c);
  configure(selectionSweep_,0,env_.SpriteTables.at(0xd0)[size_t(selected_)].Width,0x120);
  configure(chapterOpacity_,0,255,0x208);
  configure(backY_,Word(0x1ac),0,0x1b0);
  configure(closeX_,Word(0x1b4),0,0x1b8);
  configure(closeOpacity_,0,255,0x1bc);
  const auto duration=std::bit_cast<int32_t>(uint32_t(Word(0x264))*uint32_t(Word(0x268)));
  indicator_.Configure(0,Word(0x268),uint32_t(duration),true,now);
  fade_.Configure(0,255,uint32_t(Word(0x54)),false,now);
  entryEpoch_=now;
}
bool SystemMenu::AnimateEntry(uint64_t now) {
  if(backPhase_==0){backY_.Restart(now);backPhase_=1;}
  if(backPhase_==1) {
    backY_.Tick(now);
    if(backY_.Finished()){closeX_.Restart(now);closeOpacity_.Restart(now);backPhase_=2;}
  }
  if(backPhase_==2){closeX_.Tick(now);closeOpacity_.Tick(now);}
  if(entryPhase_==0){fade_.Restart(now);chapterOpacity_.Restart(now);entryPhase_=1;}
  const auto elapsed=uint32_t(now)-uint32_t(entryEpoch_);
  if(entryPhase_==1 && elapsed>=uint32_t(Word(0x7c))) {
    headerY_.Restart(now);headerOpacity_.Restart(now);entryPhase_=2;
  }
  // Original launches at most one row per update, even after a large time step.
  if(entryPhase_==2 && elapsed>=uint32_t(env_.IntegerTables.at(0x10c).at(size_t(nextRow_)))) {
    rows_[size_t(nextRow_)].Opacity.Restart(now);rows_[size_t(nextRow_)].X.Restart(now);
    if(++nextRow_==5)entryPhase_=3;
  }
  if(entryPhase_==3 && rows_[size_t(selected_)].Opacity.Finished() && rows_[size_t(selected_)].X.Finished()) {
    selectionScale_.Restart(now);selectionOpacity_.Restart(now);lineOpacity_.Restart(now);entryPhase_=4;
  }
  const auto rowsFinished=[&] {
    return std::all_of(rows_.begin(),rows_.end(),[](const auto& r){return r.Opacity.Finished()&&r.X.Finished();});
  };
  if(entryPhase_==4 && rowsFinished()){selectionWidth_.Restart(now);entryPhase_=5;}
  const bool finished=entryPhase_==5 && fade_.Finished() && headerY_.Finished() &&
    headerOpacity_.Finished() && rowsFinished() && lineOpacity_.Finished();
  // Completion observes the preceding sample; native ticks these last.
  for(auto* t:{&chapterOpacity_,&fade_,&headerY_,&headerOpacity_})t->Tick(now);
  for(auto& row:rows_){row.Opacity.Tick(now);row.X.Tick(now);}
  lineOpacity_.Tick(now);
  return finished;
}
void SystemMenu::RestartSelectionEffects(int32_t row) {
  selectionScale_.Restart(now_);
  // Native9fbde completes this channel immediately; it does not restart it.
  selectionOpacity_.Value.Current=selectionOpacity_.Value.To;
  selectionOpacity_.Value.Sample=selectionOpacity_.Value.Epoch+selectionOpacity_.Value.Duration;
  selectionWidth_.Restart(now_);
  selectionSweep_.Configure(0,env_.SpriteTables.at(0xd0).at(size_t(row)).Width,
                            uint32_t(Word(0x120)),false,now_);
  sweepStarted_=false;
}
void SystemMenu::UpdateWidgets(uint64_t now) {
  const auto update=[&](BattleCardSprite& v,std::array<int32_t,2> offset) {
    const auto events=SampleWidgetContact(v.HitRectangle(offset),contacts_.Current[0],
        contacts_.Previous[0],v.Input.TrackedId,contacts_.Velocity(0));
    v.Update(offset,events,now);contacts_.Publish(v.Input.Flags);
  };
  for(auto& row:rows_)for(auto& v:row.Variants)update(v,{row.X.Value.Current,0});
  update(close_,{closeX_.Value.Current,0});
  update(back_,{0,Negate(backY_.Value.Current)});
  update(backContact_,{0,Negate(backY_.Value.Current)});
}
int32_t SystemMenu::Update(SystemMenuInput keys,uint64_t now) {
  now_=now;
  if(pendingPhase_){phase_=*pendingPhase_;pendingPhase_.reset();}
  if(phase_==0){Setup(now);phase_=3;}
  if(phase_==3 && AnimateEntry(now))phase_=5;
  if(phase_==5) {
    // Original input precedes the current frame's contact updates.
    for(size_t i=0;i<5;++i)keys.RowReleased[i]=
      (rows_[i].Variants[rows_[i].Variant].Input.Flags&8)!=0;
    keys.BackReleased=(backContact_.Input.Flags&8)!=0;
    // The close decoration was constructed noninteractive (native E0=0).
    keys.ExitWidgetActivated=false;
    action_=UpdateSystemMenuInput(selected_,keys,*this);
    if(action_!=0) {
      if(action_!=5)return action_;
      returnPhase_=0;pendingReturnPhase_.reset();pendingPhase_=6;
    }
  }
  if(phase_==6) {
    const auto result=UpdateReturnToTitle();
    if(result==1){services_.RetireQuestion();return action_;}
    if(result==2)pendingPhase_=5;
  }
  selectionOpacity_.Tick(now);selectionScale_.Tick(now);
  selectionWidth_.Tick(now);selectionSweep_.Tick(now);
  if(selectionWidth_.Finished() && !sweepStarted_){selectionSweep_.Restart(now);sweepStarted_=true;}
  UpdateWidgets(now);
  indicator_.Tick(now);
  if(indicator_.Finished())indicator_.Restart(now);
  return 0;
}
int32_t SystemMenu::UpdateReturnToTitle() {
  // Original524c6: two distinct questions, deferred opens, same-frame save
  // start/poll, and retry on a nonaccepted save. No fabricated modal result.
  if(pendingReturnPhase_){returnPhase_=*pendingReturnPhase_;pendingReturnPhase_.reset();}
  if(returnPhase_==0)returnPhase_=1;
  if(returnPhase_==1) {
    services_.BeginQuestion(env_.Strings.at(0x27c),1);pendingReturnPhase_=2;
  }
  if(returnPhase_==2) {
    const auto q=services_.PollQuestion();
    if(!q.Busy) {
      if(q.Cancelled || q.Selection==0)returnPhase_=7;
      else if(q.Selection==1)returnPhase_=3;
    }
  }
  if(returnPhase_==3) {
    services_.BeginQuestion(env_.Strings.at(0x2b4),2);pendingReturnPhase_=4;
  }
  if(returnPhase_==4) {
    const auto q=services_.PollQuestion();
    if(!q.Busy) {
      if(q.Cancelled)returnPhase_=1;
      else if(q.Selection==1)returnPhase_=5;
      else if(q.Selection==0)returnPhase_=9;
      else returnPhase_=1;
    }
  }
  if(returnPhase_==5){services_.BeginSystemSave();returnPhase_=6;}
  if(returnPhase_==6) {
    const auto save=services_.PollSystemSave();
    if(!save.Busy)returnPhase_=save.NativeResult?9:3;
  }
  if(returnPhase_==7)returnPhase_=8;
  if(returnPhase_==8)return 2;
  if(returnPhase_==9)returnPhase_=10;
  if(returnPhase_==10)return 1;
  return 0;
}
std::vector<SystemMenuDraw> SystemMenu::Draw(int32_t chapterId,
    const std::u16string& chapterText,uint32_t utilitySize) const {
  std::vector<SystemMenuDraw> out;
  const auto blit=[&](UiSpriteBlit b) {
    UiRenderCommand c;c.Kind=UiRenderKind::Sprite;c.Sprite=std::move(b);out.emplace_back(std::move(c));
  };
  const auto sprite=[&](UiSpriteDefinition s,uint32_t texture,int32_t alpha) {
    s.Color[3]=uint8_t(alpha);blit(ResolveUiSprite(Texture(texture),s));
  };
  sprite(env_.Sprites.at(0x130),3,env_.Sprites.at(0x130).Color[3]);
  blit(close_.Draw({255,255,255,uint8_t(closeOpacity_.Value.Current)}));
  blit(back_.Draw({255,255,255,255}));
  sprite(env_.Sprites.at(0xc),0,fade_.Value.Current);
  auto line=ResolveUiSprite(Texture(24),env_.Sprites.at(0x8c));
  line.Color[3]=uint8_t(lineOpacity_.Value.Current);
  const auto pivot=env_.Points.at(0xb0);
  line.Rotation=UiSpriteRotation{0,{pivot[0],pivot[1]}};
  blit(line);
  sprite(env_.Sprites.at(0x30),1,fade_.Value.Current);
  auto header=env_.Sprites.at(0x58);header.Y=Add(header.Y,headerY_.Value.Current);
  sprite(header,2,headerOpacity_.Value.Current);
  for(size_t i=0;i<5;++i) {
    const auto& row=rows_[i];
    blit(row.Variants[row.Variant].Draw({255,255,255,uint8_t(row.Opacity.Value.Current)}));
    if(int32_t(i)!=selected_)continue;
    auto sweep=env_.SpriteTables.at(0xd0)[i];
    const auto sourceWidth=sweep.Width;sweep.Width=selectionSweep_.Value.Current;
    auto sweepDraw=ResolveUiSprite(Texture(uint32_t(17+i)),sweep);
    sweepDraw.Source[2]=sourceWidth; // Native stretches the full help texture.
    blit(sweepDraw);
    auto fill=env_.SpriteTables.at(0xe0)[i];
    fill.SourceX=Add(fill.SourceX,Add(fill.Width,Negate(selectionWidth_.Value.Current)));
    fill.Width=selectionWidth_.Value.Current;sprite(fill,22,255);
    auto glow=env_.SpriteTables.at(0xf0)[i];glow.Color[3]=uint8_t(selectionOpacity_.Value.Current);
    blit(ResolveUiSprite(Texture(23),glow,selectionScale_.Current,selectionScale_.Current));
  }
  if(chapterId!=-1) {
    sprite(env_.Sprites.at(0x1c0),4,chapterOpacity_.Value.Current);
    auto banner=env_.Sprites.at(0x1e4);
    banner.Color=env_.ColorTables.at(0x26c).at(size_t(chapterId));
    sprite(banner,5,chapterOpacity_.Value.Current);
    const float scaled=float(utilitySize)*std::bit_cast<float>(env_.Scalars.at(0x214));
    if(!std::isfinite(scaled)||scaled<0||scaled>1024)throw FormatError("invalid system menu title font size");
    const auto size=uint32_t(scaled);
    const auto alpha=uint8_t(chapterOpacity_.Value.Current);
    const auto pos=env_.Points.at(0x20c);
    const auto y=Add(pos[1],Negate(int32_t(size)));
    if(alpha)out.emplace_back(UiTextBlit{chapterText,{Add(pos[0],1),Add(y,1)},size,{0,0,0,alpha}});
    out.emplace_back(UiTextBlit{chapterText,{pos[0],y},size,{255,255,255,alpha}});
    for(int32_t i=0;i<Word(0x268);++i) {
      auto mark=env_.Sprites.at(i==indicator_.Value.Current?0x23c:0x218);
      mark.X=Add(mark.X,std::bit_cast<int32_t>(uint32_t(Word(0x260))*uint32_t(i)));
      sprite(mark,6,chapterOpacity_.Value.Current);
    }
  }
  return out;
}
}
