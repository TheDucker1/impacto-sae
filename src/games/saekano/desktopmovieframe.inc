class PcMovieFrameLayer {
 public:
  // Same Rec601 matrix as the existing OpenGL YUVFrame shader, with the
  // standalone UI consumer's varying names and uniform tint.
  UiBlitRenderer Renderer{R"GLSL(#version 330
in vec2 texturePosition;
uniform vec4 tint;
uniform sampler2D Luma;
uniform sampler2D Cb;
uniform sampler2D Cr;
out vec4 outputColor;
const mat4 conversion=mat4(
  1.16438,0.00000,1.59603,-0.87079,
  1.16438,-0.39176,-0.81297,0.52959,
  1.16438,2.01723,0.00000,-1.08139,
  0,0,0,1);
void main(){outputColor=vec4(texture(Luma,texturePosition).r,
texture(Cb,texturePosition).r,texture(Cr,texturePosition).r,1.0)*conversion*tint;}
)GLSL"};
  OpenGL::GLYUVFrame Planes;
  bool Available=false;int64_t Pts=-1;uint64_t Uploads=0;
  ~PcMovieFrameLayer(){Retire();}
  void Present(const AVFrame* frame,int64_t pts){
    if(!frame || frame->format!=AV_PIX_FMT_YUV420P || frame->width<=0 || frame->height<=0 ||
        frame->width%2 || frame->height%2)throw FormatError("unsupported movie YUV frame layout");
    if(Available && (int(Planes.Width)!=frame->width || int(Planes.Height)!=frame->height))Retire();
    if(!Available){
      Planes.Init(float(frame->width),float(frame->height));Available=true;
      for(auto id:{Planes.LumaId,Planes.CbId,Planes.CrId}){
        glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      }
    }
    glActiveTexture(GL_TEXTURE0);
    Planes.SubmitStrided(frame->data[0],frame->linesize[0],frame->data[1],frame->linesize[1],frame->data[2],frame->linesize[2]);
    Pts=pts;++Uploads;GlCheck("movie YUV plane upload");
  }
  void Draw(std::array<int32_t,4> destination={0,0,960,544},
      std::array<uint8_t,4> color={255,255,255,255},bool preserveClip=false){
    if(!Available)return;
    GLint program=0,vao=0,buffer=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
    glUseProgram(Renderer.Program);glBindVertexArray(Renderer.Vao);glBindBuffer(GL_ARRAY_BUFFER,Renderer.Buffer);
    if(!preserveClip){glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);}
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    const GLuint ids[]={Planes.LumaId,Planes.CbId,Planes.CrId};const char* names[]={"Luma","Cb","Cr"};
    for(int i=0;i<3;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,ids[i]);glUniform1i(glGetUniformLocation(Renderer.Program,names[i]),i);}
    glActiveTexture(GL_TEXTURE0);UiSpriteBlit sprite;sprite.Destination=destination;sprite.Source={0,0,int32_t(Planes.Width),int32_t(Planes.Height)};
    sprite.Color=color;Renderer.DrawQuad(sprite,{Planes.LumaId,uint32_t(Planes.Width),uint32_t(Planes.Height)},false);
    glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));glBindBuffer(GL_ARRAY_BUFFER,GLuint(buffer));GlCheck("movie fullscreen drawing");
  }
  void Retire(){if(Available){Planes.Release();Available=false;}Pts=-1;}
};
