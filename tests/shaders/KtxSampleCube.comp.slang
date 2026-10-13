TextureCube<float4> sourceTexture : register(t0, space0);
SamplerState sourceSampler : register(s1, space0);
RWTexture2D<float4> result : register(u2, space0);

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    static const float3 faces[6] = {
        float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
        float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)
    };
    result[id.xy] = sourceTexture.SampleLevel(sourceSampler, faces[id.y], float(id.x));
}
