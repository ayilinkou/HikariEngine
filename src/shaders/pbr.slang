struct PointLight
{
	float3 Pos;
	float Intensity;
	float3 Color;
	float Padding;
};

struct UniformBuffer
{
	float4x4 Model;
	float4x4 View;
	float4x4 Proj;
	float4x4 NormalMatrix;
	PointLight Light;
	float3 SphereColor;
	float Time;
	float3 CameraPos;
	float Padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBuffer> ubo;

struct VS_In
{
	float3 Pos;
	float3 Color;
	float2 TexCoord;
	float3 Normal;
	float4 Tangent;
};

struct VS_Out
{
	float4 Pos : SV_Position;
	float3 WorldPos;
	float3 Color;
	float2 TexCoord;
	float3 Normal;
	float4 Tangent;
};

static const float PI = 3.14159265359f;

[shader("vertex")]
VS_Out vertMain(VS_In v)
{
	VS_Out o;
	float4 worldPos = mul(ubo.Model, float4(v.Pos, 1.f));
	o.Pos = mul(ubo.Proj, mul(ubo.View, worldPos));
	o.WorldPos = worldPos.xyz;
	o.Color = v.Color;
	o.TexCoord = v.TexCoord;
	float3x3 inverseTransposeMatrix = (float3x3)ubo.NormalMatrix;
	o.Normal = mul(inverseTransposeMatrix, v.Normal);
	o.Tangent = float4(mul(inverseTransposeMatrix, v.Tangent.xyz), v.Tangent.w);
	return o;
}

// Trowbridge-Reitz GGX normal distribution function
float DistributionGGX(float3 N, float3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = saturate(dot(N, H));
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float denum = (NdotH2 * (a2 - 1.f) + 1.f);
	denum = PI * denum * denum;

	return num / denum;
}

float3 SchlickFresnel(float3 F0, float cosTheta)
{
	return F0 + (float3(1.f) - F0) * pow(1.f - cosTheta , 5.f);
}

// Smith Geometry function, using Schlick-GGX
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	float NdotV = saturate(dot(N, V));
	float NdotL = saturate(dot(N, L));

	float ggx1 = GeometrySchlickGGX(NdotV, roughness);
	float ggx2 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = roughness + 1.f;
	float k = (r * r) / 8.f;

	float num = NdotV;
	float denum = NdotV * (1.f - k) + k;

	return num / denum;
}

// Ks is removed from the original equation since it's already accounted for in the Fresnel
float3 CookTorrenceBRDF(float3 N, float3 V, float3 L, float3 H, float3 albedo, float roughness, float metallic)
{
	float D = DistributionGGX(N, H, roughness);

	float3 F0 = float3(0.04f);
	F0 = lerp(F0, albedo, metallic);
	float3 F = SchlickFresnel(F0, saturate(dot(H, V)));
	
	float G = GeometrySmith(N, V, L, roughness);

	float3 Ks = F;
	float3 Kd = float3(1.f, 1.f, 1.f) - Ks;
	Kd *= (1.f - metallic);

	float3 diffuse = Kd * (albedo / PI);

	float NdotV = saturate(dot(N, V));
	float NdotL = saturate(dot(N, L));
	float3 specular = (D * F * G) / max(4.f * NdotV * NdotL, 0.001f); // protect against dividing by 0

	return diffuse + specular;
}

static const float3x3 ACESInputMat =
{
	{ 0.59719, 0.35458, 0.04823 },
	{ 0.07600, 0.90834, 0.01566 },
	{ 0.02840, 0.13383, 0.83777 }
};

static const float3x3 ACESOutputMat =
{
	{  1.60475, -0.53108, -0.07367 },
	{ -0.10208,  1.10813, -0.00605 },
	{ -0.00327, -0.07276,  1.07602 }
};

float3 RRTAndODTFit(float3 color)
{
	float3 a = color * (color + 0.0245786f) - 0.000090537f;
	float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
	return a / b;
}

float3 HillACES(float3 color)
{
	float3 result = mul(ACESInputMat, color);
	result = RRTAndODTFit(result);
	result = mul(ACESOutputMat, result);

	return saturate(result);
}

// Inverse-square law
float CalcAttenuation(float3 FragPos, float3 LightPos)
{
	float Distance = distance(FragPos, LightPos);
	return 1.f / (Distance * Distance);
}

// [[vk::binding(binding, set)]]
[[vk::binding(0, 1)]]
Sampler2D albedoTex;
[[vk::binding(1, 1)]]
Sampler2D normalTex;
[[vk::binding(2, 1)]]
Sampler2D roughnessTex;
[[vk::binding(3, 1)]]
Sampler2D metallicTex;
[[vk::binding(4, 1)]]
Sampler2D aoTex;

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	// TODO: move all these to UBO
	bool bTwoSided = false;
	
	float3 albedo = albedoTex.Sample(f.TexCoord).rgb;
	float ao = aoTex.Sample(f.TexCoord).r;
	float roughness = roughnessTex.Sample(f.TexCoord).r;
	float metallic = metallicTex.Sample(f.TexCoord).r;

	float3 vertexNormal = normalize(f.Normal);
	float3 camToFrag = normalize(f.WorldPos - ubo.CameraPos);
	float viewNormalAlignment = dot(camToFrag, vertexNormal); 
	bool bNormalNeedsFlip = bTwoSided && (viewNormalAlignment > 0.f);

	float3 tangentNormal = normalTex.Sample(f.TexCoord).xyz; // packed tangent space normal
	tangentNormal = tangentNormal * 2.f - float3(1.f); // unpacked tangent space
	
	float3 T = normalize(f.Tangent.xyz);
	T = normalize(T - vertexNormal * dot(T, vertexNormal)); // re-orthogonalise tangent
	float3 B = cross(vertexNormal, T) * f.Tangent.w; // w contains the handedness

	float3x3 TBN = float3x3(T, B, vertexNormal);
	float3 N = normalize(mul(tangentNormal, TBN)); // world space normal
	
	N = bNormalNeedsFlip ? -1.f * N : N;

	float attenuation = CalcAttenuation(f.WorldPos, ubo.Light.Pos);
	float3 Li = ubo.Light.Color * ubo.Light.Intensity * attenuation; // light radiance
	float3 V = -1.f * camToFrag; // view vector
	float3 L = normalize(ubo.Light.Pos - f.WorldPos); // light vector
	float3 H = normalize(L + V); // halfway vector
	float NdotL = saturate(dot(N, L));
	float NdotH = saturate(dot(N, H));
	float3 BRDF = CookTorrenceBRDF(N, V, L, H, albedo, roughness, metallic);

	float3 Lo = float3(0.f, 0.f, 0.f);
	Lo += BRDF * Li * NdotL;
	
	float3 ambient = 0.01f * albedo * ao;
	float3 color = ambient + Lo;
	color = HillACES(color); // tone mapping
	color = pow(color, float3(1.f / 2.2f)); // gamma correction

	return float4(ambient + color, 1.f);
}
