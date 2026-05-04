struct UniformBuffer
{
	float4x4 Model;
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
	float3 pos;

	const uint INSTANCE_PER_SIDE = 4;
	const float GRID_SIZE = 10.f;
	const float START = -GRID_SIZE / 2.f;
	const float INTERVAL = GRID_SIZE / (INSTANCE_PER_SIDE - 1);
	const float Z = -8.f;

	pos = v.Pos;

	pos.x += START + (v.InstanceID % INSTANCE_PER_SIDE) * INTERVAL;
	pos.y += START + (v.InstanceID / INSTANCE_PER_SIDE) * INTERVAL;
	pos.z += Z;

	o.Pos = mul(ubo.Proj, mul(ubo.View, mul(ubo.Model, float4(pos, 1.f))));
	o.Color = v.Color;
	o.TexCoord = v.TexCoord;
	o.Normal = mul(ubo.Model, float4(v.Normal, 0.f)).xyz;
	return o;
}

Sampler2D texture;

[shader("fragment")]
float4 fragMain(VS_Out f) : SV_Target
{
	//return float4(texture.Sample(f.TexCoord).rgb * f.Color, 1.f);
	return float4(normalize(f.Normal), 1.f);
}
