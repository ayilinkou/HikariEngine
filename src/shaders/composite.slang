#include "common.slangh"

struct VS_In
{
	float2 Pos : POSITION0;
	float2 TexCoord : TEXCOORD0;
};

struct VS_Out
{
	float4 Pos : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

[shader("vertex")]
VS_Out vertMain(VS_In v)
{
	VS_Out o;
	o.Pos = float4(v.Pos, 0.f, 1.f);
	o.TexCoord = v.TexCoord;
	return o;
}

[[vk::binding(0, 1)]]
Sampler2D opaqueTex;
[[vk::binding(1, 1)]]
Sampler2D accumTex;
[[vk::binding(2, 1)]]
Sampler2D revealageTex;

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	return float4(opaqueTex.Sample(f.TexCoord).rgb, 1.f);
}
