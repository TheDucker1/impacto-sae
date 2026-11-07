#include "SceneHalf.hlsl"
sampler2D image:register(s0);
float4 alpha:register(c0); // comparison family and reference, before half storage
float4 main(float4 color:COLOR0,float2 uv:TEXCOORD0):COLOR0 {
  float4 value=color*tex2D(image,uv);
  bool alphaPass=true;
  if(alpha.x==1)alphaPass=value.a==alpha.y;
  else if(alpha.x==2)alphaPass=value.a!=alpha.y;
  else if(alpha.x==3)alphaPass=value.a<alpha.y;
  else if(alpha.x==4)alphaPass=value.a<=alpha.y;
  else if(alpha.x==5)alphaPass=value.a>alpha.y;
  else if(alpha.x==6)alphaPass=value.a>=alpha.y;
  if(!alphaPass)discard;
  return HalfRound4(value);
}
