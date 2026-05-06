struct PointLight
{
	float3 Pos;
	float Intensity;
	float3 Color;
	float Padding;
};

struct UniformBuffer
{
	float4x4 Models[16];
	float4x4 View;
	float4x4 Proj;
	PointLight Light;
	float3 SphereColor;
	float Time;
};

ConstantBuffer<UniformBuffer> ubo;

struct VS_In
{
	uint InstanceID : SV_InstanceID;
	float3 Pos;
	float3 Color;
	float2 TexCoord;
	float3 Normal;
};

struct VS_Out
{
	float4 Pos : SV_Position;
	float3 WorldPos;
	float3 Color;
	float2 TexCoord;
	float3 Normal;
};

[shader("vertex")]
VS_Out vertMain(VS_In v)
{
	VS_Out o;
	float4 worldPos = mul(ubo.Models[v.InstanceID], float4(v.Pos, 1.f));
	o.Pos = mul(ubo.Proj, mul(ubo.View, worldPos));
	o.WorldPos = worldPos.xyz;
	o.Color = v.Color;
	o.TexCoord = v.TexCoord;
	o.Normal = mul(ubo.Models[v.InstanceID], float4(v.Normal, 0.f)).xyz;
	return o;
}

Sampler2D texture;

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	float3 fragColor = ubo.SphereColor;

	float3 ambient = 0.02f * fragColor;
	float3 diffuse = float3(0.f, 0.f, 0.f);

	float3 fragToLight = normalize(ubo.Light.Pos - f.WorldPos);
	float NdotL = saturate(dot(f.Normal, fragToLight));
	diffuse = ubo.Light.Color * ubo.Light.Intensity * fragColor * NdotL;

	float3 finalColor = ambient + diffuse;

	return float4(finalColor, 1.f);
}
