#include "yuvframe.h"
#include "../../log.h"

#include <glad/glad.h>

namespace Impacto {
namespace OpenGL {

void GLYUVFrame::Init(float width, float height) {
  Width = width;
  Height = height;
  GLuint yuv[3];

  glGenTextures(3, yuv);
  LumaId = yuv[0];
  CbId = yuv[1];
  CrId = yuv[2];
}

void GLYUVFrame::Submit(const void* luma, const void* cb, const void* cr) {
  glBindTexture(GL_TEXTURE_2D, LumaId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)Width, (GLsizei)Height, 0,
               GL_RED, GL_UNSIGNED_BYTE, luma);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindTexture(GL_TEXTURE_2D, CbId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)(Width / 2),
               (GLsizei)(Height / 2), 0, GL_RED, GL_UNSIGNED_BYTE, cb);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindTexture(GL_TEXTURE_2D, CrId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)(Width / 2),
               (GLsizei)(Height / 2), 0, GL_RED, GL_UNSIGNED_BYTE, cr);
  glGenerateMipmap(GL_TEXTURE_2D);
}

void GLYUVFrame::Release() {
  GLuint yuv[3];
  yuv[0] = LumaId;
  yuv[1] = CbId;
  yuv[2] = CrId;
  glDeleteTextures(3, yuv);
}

void GLYUVFrame::SubmitStrided(const void* luma,int lumaStride,const void* cb,int cbStride,const void* cr,int crStride){
  if(lumaStride<int(Width) || cbStride<int(Width)/2 || crStride<int(Width)/2)
    throw std::invalid_argument("YUV plane stride is smaller than its visible width");
  GLint alignment=0,rowLength=0;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rowLength);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  const GLuint ids[]={LumaId,CbId,CrId};const void* planes[]={luma,cb,cr};const int strides[]={lumaStride,cbStride,crStride};
  for(int i=0;i<3;++i){
    glBindTexture(GL_TEXTURE_2D,ids[i]);glPixelStorei(GL_UNPACK_ROW_LENGTH,strides[i]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,GLsizei(Width)/(i?2:1),GLsizei(Height)/(i?2:1),0,GL_RED,GL_UNSIGNED_BYTE,planes[i]);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,rowLength);
}

}  // namespace OpenGL
}  // namespace Impacto
