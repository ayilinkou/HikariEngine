#include "TextureReview.slangh"

Texture2D<float4> sourceTexture : register(t0, space0);

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    // Rendering the whole texture at 1/4 or 1/16 size exercises implicit mip selection.
    return float4(DisplayColor(sourceTexture.Sample(sourceSampler, input.Uv)), 1.f);
}
