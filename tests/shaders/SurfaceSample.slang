struct VertexInput
{
    float2 Position : POSITION0;
    float2 Uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

Texture2D<float4> sourceTexture : register(t0, space0);
SamplerState sourceSampler : register(s1, space0);

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.Position = float4(input.Position, 0.f, 1.f);
    output.Uv = input.Uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    // Implicit derivatives exercise the renderer's minification policy.
    return sourceTexture.Sample(sourceSampler, input.Uv);
}
