#include "common.slangh"

struct MaterialPushConstant
{
	float4 Albedo;
	float Metallic;
	float Roughness;
	float AO;
	float Opacity;
	bool bHasAlbedoTex;
	bool bHasNormalTex;
	bool bHasMetallicRoughnessTex;
	bool bTwoSided;
};

struct VS_In
{
	// per vertex
	float3 Pos : POSITION0;
	float2 TexCoord : TEXCOORD0;
	float3 Normal : NORMAL0;
	float4 Tangent : TANGENT0;

	// per instance
	// transposed so that float4x4 constructor makes a row major matrix
	float4 TModelCol0 : POSITION1;
	float4 TModelCol1 : POSITION2;
	float4 TModelCol2 : POSITION3;
	float4 TModelCol3 : POSITION4;
	
	float4 TNormalCol0 : NORMAL1;
	float4 TNormalCol1 : NORMAL2;
	float4 TNormalCol2 : NORMAL3;
	float4 TNormalCol3 : NORMAL4;

	float4x4 GetModelMatrix()
	{
		return float4x4(TModelCol0, TModelCol1, TModelCol2, TModelCol3);
	}

	float4x4 GetNormalMatrix()
	{
		return float4x4(TNormalCol0, TNormalCol1, TNormalCol2, TNormalCol3); // TODO: only upload 3x3 matrix
	}
};

struct VS_Out
{
	float4 Pos : SV_Position;
	float3 WorldPos : TEXCOORD0;
	float3 Color : TEXCOORD1;
	float2 TexCoord : TEXCOORD2;
	float3 Normal : TEXCOORD3;
	float4 Tangent : TEXCOORD4;
};

static const float PI = 3.14159265359f;

[shader("vertex")]
VS_Out vertMain(VS_In v)
{
	VS_Out o;
	float4 worldPos = mul(float4(v.Pos, 1.f), v.GetModelMatrix());
	o.Pos = mul(mul(worldPos, ubo.View), ubo.Proj);
	o.WorldPos = worldPos.xyz;
	o.TexCoord = v.TexCoord;
	float3x3 normalMatrix = (float3x3)v.GetNormalMatrix();
	o.Normal = mul(v.Normal, normalMatrix);
	o.Tangent = float4(mul(v.Tangent.xyz, normalMatrix), v.Tangent.w);
	return o;
}

// [[vk::binding(binding, set)]]
[[vk::binding(0, 1)]]
Sampler2D albedoTex;
[[vk::binding(1, 1)]]
Sampler2D normalTex;
[[vk::binding(2, 1)]]
Sampler2D metallicRoughnessTex;
[[vk::push_constant]]
MaterialPushConstant pcMatData;

struct FS_Out
{
	float4 Accum : SV_Target0;
	float Revealage : SV_Target1;
};

// based on https://casual-effects.blogspot.com/2015/03/implemented-weighted-blended-order.html
[shader("fragment")]
FS_Out fragMain(VS_Out f)
{
	FS_Out o;
	
	float opacity = pcMatData.Opacity;
	float3 baseColor = float3(0.8f, 0.8f, 0.8f);
	float4 premultipliedReflect = float4(baseColor * opacity, opacity);
	float3 transmit = float3(0.f, 0.f, 0.f);

    premultipliedReflect.a *= 1.f - clamp((transmit.r + transmit.g + transmit.b) * (1.f / 3.f), 0.f, 1.f);

    float a = min(1.f, premultipliedReflect.a) * 8.f + 0.01f;
    //float b = -gl_FragCoord.z * 0.95f + 1.f;
    float b = 0.5f * 0.95f + 1.f; // temp

    // If your scene has a lot of content very close to the far plane,
    //   then include this line (one rsqrt instruction):
    //   b /= sqrt(1e4 * abs(csZ));
    float w    = clamp(a * a * a * 1e8f * b * b * b, 1e-2f, 3e2f);
    o.Accum     = premultipliedReflect * w;
    o.Revealage = premultipliedReflect.a;

	return o;
}
