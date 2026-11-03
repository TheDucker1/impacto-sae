// Included after SceneDialogLayer. This consumes the verified Options command
// producers without re-creating layout, widget variants, font sizes or ordering.
class PcOptionsLayer {
 public:
  struct Counts {size_t Sprites=0,Rectangles=0,Texts=0,Widgets=0,Pages=0,HelpRows=0,Questions=0,Saves=0;};
  PcOptionsLayer(SceneDialogLayer& shared,OptionsEnvironment environment,
      ButtonHelpEnvironment help,ButtonHelpStyle style,uint32_t utilitySize)
      :shared_(shared),env_(std::move(environment)),help_(std::move(help)),style_(std::move(style)),size_(utilitySize){}
  const OptionsEnvironment& Environment()const{return env_;}
  // The enclosing title frame owns viewport/clear. The header is an explicit
  // owner input; highlight alpha comes from the actual owner tween.
  Counts Draw(const OptionsOwnerState& owner,const OptionsWidgets& widgets,bool alternate,
      OptionsDialogHost& dialogs,std::ostream* commands=nullptr){
    Counts count;
    const auto text=[&](const UiTextBlit& t){
      if(commands){WriteDescription(*commands,-1,t);*commands<<'\n';}
      shared_.Textures.DrawString(shared_.Renderer,shared_.Font,t.Text,t.Position[0],t.Position[1],t.Size,t.Color);++count.Texts;
    };
    const auto primitive=[&](const UiRenderCommand& c){
      if(commands){WriteUiCommand(*commands,c);*commands<<'\n';}
      if(c.Kind==UiRenderKind::Sprite){shared_.Sprite(c.Sprite);++count.Sprites;}
      else {shared_.Renderer.Execute(std::span<const UiRenderCommand>(&c,1));++count.Rectangles;}
    };
    const auto sprite=[&](const UiSpriteBlit& s){UiRenderCommand c;c.Kind=UiRenderKind::Sprite;c.Sprite=s;primitive(c);};
    const auto widget=[&](OptionsWidgetId id){for(const auto& c:widgets.Draw(id))primitive(c);++count.Widgets;};
    auto input=OptionsDrawFromState(owner,alternate,dialogs.Question.Visible,
        {help_.OptionsRectangle[0],help_.OptionsRectangle[1]});
    for(const auto& c:BuildOptionsDraw(env_,input))switch(c.Kind){
      case OptionsDrawKind::Sprite:sprite(c.Sprite);break;
      case OptionsDrawKind::Widget:
        widget({-1,c.Index==4?OptionsWidgetKind::Revert:OptionsWidgetKind::Tab,c.Index==4?0u:c.Index});break;
      case OptionsDrawKind::Decoration:
        for(const auto& t:BuildButtonHelp(help_.OptionsRows.at(size_t(owner.PreviewHint)),help_.OptionsRectangle,
            style_,shared_.Font.Cached(),size_,shared_.Font.NominalSize()))text(t);
        ++count.HelpRows;break;
      case OptionsDrawKind::Page:{
        auto page=OptionsPageDrawFromState(owner,uint8_t(owner.Highlight.Channel.Current));page.Page=c.Index;
        for(const auto& p:BuildOptionsPageDraw(env_,page,shared_.Font.Cached(),size_,shared_.Font.NominalSize())){
          if(p.Kind==OptionsPageDrawKind::Sprite)sprite(p.Sprite);
          else if(p.Kind==OptionsPageDrawKind::Text)text(p.Text);
          else widget(p.Widget);
        }
        ++count.Pages;break;
      }
      case OptionsDrawKind::Question:
        shared_.Draw(dialogs.Question,dialogs.Prompt(),dialogs.Layout(),2);++count.Questions;break;
      case OptionsDrawKind::Save:
        shared_.Draw(dialogs.Save.Modal,dialogs.Save.Prompt(),dialogs.Save.Layout(),0);
        if(dialogs.Save.Modal.Visible)++count.Saves;break;
      case OptionsDrawKind::Overlay:{
        UiRenderCommand p;p.Kind=UiRenderKind::Rectangle;p.Rectangle=c.Rectangle;p.Color=c.Color;primitive(p);break;
      }
    }
    GlCheck("Options frame composition");return count;
  }
 private:
  SceneDialogLayer& shared_;OptionsEnvironment env_;ButtonHelpEnvironment help_;ButtonHelpStyle style_;uint32_t size_;
};
