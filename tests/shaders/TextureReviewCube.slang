#include "TextureReview.slangh"

TextureCube<float4> sourceTexture : register(t0, space0);

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    float2 uv = input.Uv * 2.f - float2(1.f);
    float3 direction;
    switch (review.Face)
    {
        case 0: direction = float3(1.f, -uv.y, -uv.x); break;
        case 1: direction = float3(-1.f, -uv.y, uv.x); break;
        case 2: direction = float3(uv.x, 1.f, uv.y); break;
        case 3: direction = float3(uv.x, -1.f, -uv.y); break;
        case 4: direction = float3(uv.x, -uv.y, 1.f); break;
        default: direction = float3(-uv.x, -uv.y, -1.f); break;
    }
    return float4(DisplayColor(sourceTexture.Sample(sourceSampler, direction)), 1.f);
}
