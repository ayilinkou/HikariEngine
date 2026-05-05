struct UniformBuffer
{
	float4x4 Models[16];
	float4x4 View;
	float4x4 Proj;
};

ConstantBuffer<UniformBuffer> ubo;

struct PointLight
{
	float3 Pos;
	float3 Color;
	float Intensity;
};

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
	float3 fragColor = f.Color; // can replace with texture later

	PointLight pointLight;
	pointLight.Pos = float3(30.f, 0.f, -8.f);
	pointLight.Color = float3(1.f, 1.f, 1.f);
	pointLight.Intensity = 1.f;

	float3 ambient = 0.02f * fragColor;
	float3 diffuse = float3(0.f, 0.f, 0.f);

	float3 fragToLight = normalize(pointLight.Pos - f.WorldPos);
	float NdotL = saturate(dot(f.Normal, fragToLight));
	diffuse = pointLight.Color * pointLight.Intensity * fragColor * NdotL;

	float3 finalColor = ambient + diffuse;

	return float4(finalColor, 1.f);
}
