#include "ending.h"
#include "endingdialog.h"
#include "commands.h"
#include "chapter.h"

namespace Impacto::Saekano {
EndingEnvironment EndingEnvironment::Decode(std::span<const uint8_t> bytes){
  const auto chapter=ChapterEnvironment::Decode(bytes);
  PayloadReader r(bytes.subspan(chapter.PrefixBytes));
  const auto menus=r.U32();r.CheckCount(menus,8);
  for(uint32_t i=0;i<menus;++i){
    r.String();const auto children=r.U32();r.CheckCount(children,12);
    for(uint32_t j=0;j<children;++j){r.I32();r.String();r.U32();}
  }
  // 607cc scalar,607d0 rectangle,607e0/7e8 points,607f0/7f4 scalars,
  // and607f8/60800 points. Native serialized fields, not authored offsets.
  EndingEnvironment result;
  result.ParticleOpacityGroups=r.U32();
  for(size_t i=1;i<15;++i)r.U32();
  result.ClearSavePrompt=r.String();result.DiscardClearPrompt=r.String();result.SystemSavePrompt=r.String();
  result.PrefixBytes=bytes.size()-r.Remaining();return result;
}
void StartScenarioEnding(ScenarioEndingState& state){
  if(state.Started && !state.TitleRequested)throw FormatError("scenario ending is already active");
  const auto fade=state.Fade;state={};state.Fade=fade;state.Started=true;
}
void UpdateScenarioEnding(ScenarioEndingState& s,ScenarioEndingServices& host,
                         uint64_t now,std::optional<int32_t> fadeDuration){
  if(!s.Started)throw UnsupportedCommand("scenario ending Start was suppressed");
  if(s.PendingPhase){s.Phase=*s.PendingPhase;s.PendingPhase.reset();}
  if(s.Phase<22 || s.Phase>26)throw UnsupportedCommand("unsupported scenario ending phase");
  if(s.Phase==22){host.OpenPrompt(now);s.Phase=23;}
  if(s.Phase==23){
    const auto dialog=host.UpdatePrompt(now);
    if(!dialog.Visible){
      host.RetirePrompt();
      if(dialog.Result!=1){s.Fade.Start(255,255,1,now);s.PendingPhase=26;return;}
      host.BeginEndingSave(now);s.Phase=24;
    }
  }
  if(s.Phase==24){
    const auto save=host.UpdateEndingSave(now);
    if(!save.Active){
      if(!save.Accepted)s.Phase=22;
      else{s.Fade.Start(255,255,1,now);s.PendingPhase=26;}
    }
  }
  if(s.Phase==25){
    if(!fadeDuration)throw UnsupportedCommand("ending title fade duration is unavailable");
    if(*fadeDuration<0)throw FormatError("negative ending title fade duration");
    s.Fade.Start(1,255,*fadeDuration,now);s.Phase=26;
  }
  if(s.Phase==26){
    s.Fade.Tick(now);
    if(s.Fade.Finished()){host.RequestTitle();s.TitleRequested=true;}
  }
}
ScenarioEndingDialogHost::ScenarioEndingDialogHost(std::u16string prompt,DialogContentLayout layout,size_t buttons,
    EndingSaveServices& saves,std::function<void()> requestTitle):prompt_(std::move(prompt)),layout_(layout),
    buttons_(buttons),saves_(saves),requestTitle_(std::move(requestTitle)){
  if(prompt_.empty() || !buttons_ || buttons_>3 || !requestTitle_)
    throw FormatError("ending modal resources or title service are missing");
}
void ScenarioEndingDialogHost::OpenPrompt(uint64_t now){
  BeginDialog(Modal,layout_,buttons_,0,false,true,now);Input={};owned_=true;
}
EndingDialogStatus ScenarioEndingDialogHost::UpdatePrompt(uint64_t now){
  if(!owned_)throw UnsupportedCommand("ending modal is unavailable");
  UpdateDialog(Modal,Input,now);Input={};return {Modal.Visible,Modal.Result};
}
void ScenarioEndingDialogHost::RetirePrompt(){Modal={};Input={};owned_=false;}
void ScenarioEndingDialogHost::BeginEndingSave(uint64_t now){saves_.Begin(2,now);}
EndingSaveStatus ScenarioEndingDialogHost::UpdateEndingSave(uint64_t now){return saves_.Update(now);}
void ScenarioEndingDialogHost::RequestTitle(){requestTitle_();}
}
