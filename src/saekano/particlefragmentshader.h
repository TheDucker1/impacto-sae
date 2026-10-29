#pragma once
namespace Impacto::Saekano {
// GXP80257a00, SHAa13ead345fd496cface51a3fb90848e89adbf4892f168cdb3f833fb34eb9840e.
inline constexpr char ParticleMode0FragmentShader[] = R"SAEFRAG(#version 330
#extension GL_ARB_shading_language_packing : require
in vec2 texturePosition;
uniform sampler2D image;
uniform vec4 tint;
out vec4 outputColor;
void main(){
  vec4 value=texture(image,texturePosition)*tint;
  outputColor=vec4(unpackHalf2x16(packHalf2x16(value.xy)),unpackHalf2x16(packHalf2x16(value.zw)));
}
)SAEFRAG";
}
