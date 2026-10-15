Texture2D<float4> sourceTexture : register(t0, space0);
SamplerState sourceSampler : register(s1, space0);
RWTexture2D<float4> result : register(u2, space0);

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Each column explicitly samples a different mip. Rows probe the four
    // quadrants, revealing flipped orientations as well as the wrong level.
    float2 uv = float2((id.y & 1) != 0 ? 0.75 : 0.25, (id.y & 2) != 0 ? 0.75 : 0.25);
    result[id.xy] = sourceTexture.SampleLevel(sourceSampler, uv, float(id.x));
}
