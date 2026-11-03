#include "SceneHalf.hlsl"
sampler2D image:register(s0);
sampler2D ruleMask:register(s1);
float4 tint:register(c0);
// solid, glyph, alpha glyph, colored rule
float4 flags:register(c1);
// image rule, rule value, filter mode, filter intensity
float4 effect:register(c2);
float4 alphaMode:register(c3);
float4 main(float2 uv:TEXCOORD0):COLOR0 {
  float4 sample=tex2D(image,uv);
  float4 result=flags.x>0.5?tint:sample*tint;
  if(flags.z>0.5)result=HalfRound4(tint*float4(1,1,1,sample.a));
  else if(effect.x>0.5)result=HalfRound4(tint*sample*float4(1,1,1,saturate(tex2D(ruleMask,uv).a+effect.y)));
  else if(flags.w>0.5)result=HalfRound4(float4(tint.rgb,saturate(sample.a+effect.y)));
  else if(effect.z>0.5 && flags.x<0.5 && flags.y<0.5) {
    float value=saturate(effect.w);
    float luminance=dot(float3(0.298911988735199,0.5866109728813171,0.11447799950838089),result.rgb);
    float3 rgb=mad((value*luminance).xxx,float3(0.9411764740943909,0.7843137383460999,0.5686274766921997),result.rgb*(1-value));
    result=HalfRound4(tint*float4(rgb,result.a));
  }
  if((alphaMode.x==6 && result.a<=0) || (alphaMode.x==3 && result.a==0))discard;
  return result;
}
