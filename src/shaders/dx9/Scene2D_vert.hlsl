// XY is the pixel center offset and ZW is half the logical canvas size.
float4 sceneTransform : register(c0);
struct Output { float4 Position:POSITION0; float2 Uv:TEXCOORD0; };
Output main(float2 position:POSITION0,float2 uv:TEXCOORD0) {
  Output o;
  o.Position=float4(position.x/sceneTransform.z-1.0,1.0-position.y/sceneTransform.w,0.5,1.0);
  o.Position.xy+=sceneTransform.xy;
  o.Uv=uv;return o;
}
