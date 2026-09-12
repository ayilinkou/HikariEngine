#include "common.slangh"
#include "pbr.slangh"

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

// The sampler is s4 rather than s0: the index must be unique across register
// classes within a space, because Vulkan has one binding namespace per
// descriptor set where HLSL has four (plan D29).
Texture2D<float4> opaqueTex : register(t0, space1);
Texture2D<float4> accumTex : register(t1, space1);
Texture2D<float> revealageTex : register(t2, space1);
Texture2D<float4> cloudsTex : register(t3, space1);
// Separate from the texture rather than a Sampler2D: D3D12 has no combined
// image sampler, so the RHI's binding model has none either.
SamplerState cloudsSampler : register(s4, space1);

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	int3 texel = int3((int2)f.Pos.xy, 0);
	float4 opaque = float4(opaqueTex.Load(texel).rgb, 1.f);
	float4 clouds = cloudsTex.Sample(cloudsSampler, f.TexCoord); // quarter res
	
	float3 composite;
	float3 srcColor = clouds.rgb * clouds.a + opaque.rgb * (1.f - clouds.a);
	float revealage = revealageTex.Load(texel);
	if (revealage == 1.f)
	{
		composite = srcColor;
	}
	else
	{
		float4 accum = accumTex.Load(texel);
		float4 accumAbs = abs(accum);
		float maxComponent = max(max(accumAbs.r, accumAbs.g), accumAbs.b);

		if (isinf(maxComponent))
			accum.rgb = float3(accum.a);

		float epsilon = 0.00001f;
		float3 averageColor = accum.rgb / max(accum.a, epsilon);

		composite = averageColor * (1.f - revealage) + srcColor * revealage;
	}

	composite = HillACES(composite); // tone mapping
	composite = pow(composite, float3(1.f / 2.2f)); // gamma correction

	return float4(composite, 1.f);
}
