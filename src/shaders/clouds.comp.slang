#include "common.slangh"

[[vk::binding(0, 1)]] Texture2D<float> depthTexture;
[[vk::binding(0, 2)]] RWTexture2D<float4> outputImage;

struct PushConstants
{
	float3 windVelocity;
    float minHeight;
    float maxHeight;
    float coverage;
    uint stepCount;
};
[[vk::push_constant]] PushConstants pc;

float hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(
        lerp(lerp(hash(i + float3(0,0,0)), hash(i + float3(1,0,0)), f.x),
             lerp(hash(i + float3(0,1,0)), hash(i + float3(1,1,0)), f.x), f.y),
        lerp(lerp(hash(i + float3(0,0,1)), hash(i + float3(1,0,1)), f.x),
             lerp(hash(i + float3(0,1,1)), hash(i + float3(1,1,1)), f.x), f.y),
        f.z);
}

float sampleDensity(float3 pos)
{
    float3 samplePos = pos * 0.001 + pc.windVelocity * globalBuffer.Time;
    float n = noise(samplePos) * 0.5 + noise(samplePos * 2.0) * 0.25 + noise(samplePos * 4.0) * 0.25;

    float h = saturate((pos.y - pc.minHeight) / (pc.maxHeight - pc.minHeight));
    float heightGradient = smoothstep(0.0, 0.1, h) * smoothstep(1.0, 0.8, h);

    return saturate(n - pc.coverage) * heightGradient;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint2 pixel = dispatchId.xy;
    uint2 outSize;
    outputImage.GetDimensions(outSize.x, outSize.y);
    if (pixel.x >= outSize.x || pixel.y >= outSize.y) return;

    float2 uv = (float2(pixel) + 0.5) / float2(outSize);
    float2 ndc = uv * 2.0 - 1.0;
    //ndc.y = -ndc.y;

    float4 nearPoint = mul(globalBuffer.InvViewProj, float4(ndc, 0.0, 1.0));
    float4 farPoint  = mul(globalBuffer.InvViewProj, float4(ndc, 1.0, 1.0));
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    float3 rayOrigin = globalBuffer.CameraPos;
    float3 rayDir = normalize(farPoint.xyz - nearPoint.xyz);

	uint2 depthSize;
	depthTexture.GetDimensions(depthSize.x, depthSize.y);
	uint2 depthCoord = uint2(uv * float2(depthSize));
	depthCoord = min(depthCoord, depthSize - 1);

    float rawDepth = depthTexture.Load(int3(depthCoord, 0));
    float sceneDist;
    if (rawDepth >= 1.0) {
        sceneDist = 1e9; // sky, no occlusion
    } else {
        float4 worldPos = mul(globalBuffer.InvViewProj, float4(ndc, rawDepth, 1.0));
        worldPos /= worldPos.w;
        sceneDist = length(worldPos.xyz - rayOrigin);
    }

    // Ray-slab intersection
    float tMinPlane = (pc.minHeight - rayOrigin.y) / rayDir.y;
    float tMaxPlane = (pc.maxHeight - rayOrigin.y) / rayDir.y;
    float tNear = min(tMinPlane, tMaxPlane);
    float tFar  = max(tMinPlane, tMaxPlane);

    tNear = max(tNear, 0.0);
    tFar = min(tFar, sceneDist);

    if (tFar <= tNear || rayDir.y == 0.0) {
        outputImage[pixel] = float4(0, 0, 0, 0);
        return;
    }

    float stepSize = (tFar - tNear) / float(pc.stepCount);
    float transmittance = 1.0;
    float t = tNear;

    for (uint i = 0; i < pc.stepCount; ++i) {
        float3 samplePos = rayOrigin + rayDir * t;
        float density = sampleDensity(samplePos);

        if (density > 0.0) {
            transmittance *= exp(-density * stepSize * 0.01);
        }

        if (transmittance < 0.01) break;
        t += stepSize;
    }

    outputImage[pixel] = float4(1.0, 1.0, 1.0, 1.0 - transmittance);
}
