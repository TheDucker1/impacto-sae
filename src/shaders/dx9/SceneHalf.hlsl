// Shader model 3 has no half packing instruction. Round the stored values to
// binary16, including subnormals and ties to even, before framebuffer conversion.
float HalfRound(float value) {
  float a=abs(value);
  if(a==0.0)return value;
  float step=exp2(max(floor(log2(a)),-14.0)-10.0);
  float scaled=a/step;
  float lo=floor(scaled),fraction=scaled-lo;
  float rounded=lo+(fraction>0.5 || (fraction==0.5 && fmod(lo,2.0)!=0.0));
  return sign(value)*rounded*step;
}
float4 HalfRound4(float4 v){return float4(HalfRound(v.x),HalfRound(v.y),HalfRound(v.z),HalfRound(v.w));}
