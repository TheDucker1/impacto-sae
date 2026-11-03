float4 clipOffset : register(c0);
struct Output { float4 Position:POSITION0; float2 Uv:TEXCOORD0; };
Output main(float2 position:POSITION0,float2 uv:TEXCOORD0) {
  Output o;
  o.Position=float4(position.x/480.0-1.0,1.0-position.y/272.0,0.5,1.0);
  o.Position.xy+=clipOffset.xy;
  o.Uv=uv;return o;
}
