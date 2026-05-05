struct UniformBuffer
{
	float4x4 Models[16];
	float4x4 View;
	float4x4 Proj;
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
	float3 Color;
	float2 TexCoord;
	float3 Normal;
};

[shader("vertex")]
VS_Out vertMain(VS_In v)
{
	VS_Out o;
	o.Pos = mul(ubo.Proj, mul(ubo.View, mul(ubo.Models[v.InstanceID], float4(v.Pos, 1.f))));
	o.Color = v.Color;
	o.TexCoord = v.TexCoord;
	o.Normal = mul(ubo.Models[v.InstanceID], float4(v.Normal, 0.f)).xyz;
	return o;
}

Sampler2D texture;

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	//return float4(texture.Sample(f.TexCoord).rgb * f.Color, 1.f);
	return float4(normalize(f.Normal), 1.f);
}
