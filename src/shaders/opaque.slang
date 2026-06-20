// Matrices in Slang are row-major by default, just like HLSL. GLM stores
// matrices in column-major order. In order to keep things consistent, all
// matrices that are sent to the GPU will be transposed into row-major.

#include "common.slangh"
#include "pbr.slangh"

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

[shader("vertex")]
VS_Out vertMain(VS_In v)
{
	VS_Out o;
	float4 worldPos = mul(float4(v.Pos, 1.f), v.GetModelMatrix());
	o.Pos = mul(mul(worldPos, globalBuffer.View), globalBuffer.Proj);
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

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	float4 albedo;
	if (pcMatData.bHasAlbedoTex)
		albedo = albedoTex.Sample(f.TexCoord);
	else
		albedo = pcMatData.Albedo;
	
	if (albedo.a < 0.01f) // might need to play around with this number
		discard;

	float metallic, roughness;
	if (pcMatData.bHasMetallicRoughnessTex)
	{
		metallic = metallicRoughnessTex.Sample(f.TexCoord).b;
		roughness = metallicRoughnessTex.Sample(f.TexCoord).g;
	}
	else
	{
		metallic = pcMatData.Metallic;
		roughness = pcMatData.Roughness;
	}

	float ao = pcMatData.AO;
	float3 vertexNormal = normalize(f.Normal);

	float3 N;
	if (pcMatData.bHasNormalTex)
	{
		float3 tangentNormal = normalTex.Sample(f.TexCoord).xyz; // packed tangent space normal
		tangentNormal = tangentNormal * 2.f - float3(1.f); // unpacked tangent space
	
		float3 T = normalize(f.Tangent.xyz);
		T = normalize(T - vertexNormal * dot(T, vertexNormal)); // re-orthogonalise tangent
		float3 B = cross(vertexNormal, T) * f.Tangent.w; // w contains the handedness

		float3x3 TBN = float3x3(T, B, vertexNormal);
		N = normalize(mul(tangentNormal, TBN)); // world space normal
	}
	else
	{
		N = vertexNormal;
	}

	float3 camToFrag = normalize(f.WorldPos - globalBuffer.CameraPos);
	float viewNormalAlignment = dot(camToFrag, vertexNormal); 
	bool bNormalNeedsFlip = pcMatData.bTwoSided && (viewNormalAlignment > 0.f);
	N = bNormalNeedsFlip ? -1.f * N : N;
	
	float attenuation = CalcAttenuation(f.WorldPos, globalBuffer.Light.Pos);
	float3 Li = globalBuffer.Light.Color * globalBuffer.Light.Intensity * attenuation; // light radiance
	float3 V = -1.f * camToFrag; // view vector
	float3 L = normalize(globalBuffer.Light.Pos - f.WorldPos); // light vector
	float3 H = normalize(L + V); // halfway vector
	float NdotL = saturate(dot(N, L));
	float NdotH = saturate(dot(N, H));
	
	float3 diffuse, specular;
	CookTorrenceBRDF(N, V, L, H, albedo.rgb, roughness, metallic, diffuse, specular);
	float3 BRDF = diffuse + specular;

	float3 Lo = float3(0.f, 0.f, 0.f);
	Lo += BRDF * Li * NdotL;
	
	float3 ambient = 0.05f * albedo.rgb * ao;
	float3 color = ambient + Lo;

	// TODO: These will need to be moved to a dedicated post process pass as linear
	// color data will be needed for the composite pass.
	color = HillACES(color); // tone mapping
	color = pow(color, float3(1.f / 2.2f)); // gamma correction

	return float4(color, albedo.a);
}
