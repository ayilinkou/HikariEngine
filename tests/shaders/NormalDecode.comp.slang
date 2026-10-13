#include "../../engine/engine/src/shaders/normal.slangh"
#include "../../engine/engine/src/shaders/registers.slangh"

Texture2D<float4> sourceTexture : register(t0, space0);
SamplerState sourceSampler : register(s1, space0);
RWTexture2D<float4> result : register(u2, space0);
PUSH_CONSTANT(MaterialPushConstant, material);

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float4 packed = sourceTexture.SampleLevel(sourceSampler, float2(0.5f), float(id.x));
    float3 normal = DecodeTangentNormal(packed, material.NormalEncoding);
    result[id.xy] = float4(normal * 0.5f + float3(0.5f), 1.f);
}
