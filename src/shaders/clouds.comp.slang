#include "common.slangh"
#include "pbr.slangh"

[[vk::binding(0, 1)]] Texture2D<float> depthTexture;
[[vk::binding(0, 2)]] RWTexture2D<float4> outputImage;
[[vk::binding(1, 2)]] Texture3D<float> perlinWorley;

struct PushConstants
{
	float3 windVelocity;
    float minHeight;
    float maxHeight;
    float coverage;
	float anisotropy;
    uint viewStepCount;
	uint sunStepCount;
};
[[vk::push_constant]] PushConstants pc;

float hash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// TODO: swap to perlin-worley noise texture
float noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.f - 2.f * f);
    return lerp(
        lerp(lerp(hash(i + float3(0.f,0.f,0.f)), hash(i + float3(1.f,0.f,0.f)), f.x),
             lerp(hash(i + float3(0.f,1.f,0.f)), hash(i + float3(1.f,1.f,0.f)), f.x), f.y),
        lerp(lerp(hash(i + float3(0.f,0.f,1.f)), hash(i + float3(1.f,0.f,1.f)), f.x),
             lerp(hash(i + float3(0.f,1.f,1.f)), hash(i + float3(1.f,1.f,1.f)), f.x), f.y),
        f.z);
}

float SampleDensity(float3 pos)
{
    float3 samplePos = pos * 0.001f + pc.windVelocity * globalBuffer.Time;
    float n = noise(samplePos) * 0.5f + noise(samplePos * 2.f) * 0.25f + noise(samplePos * 4.f) * 0.25f;

    float h = saturate((pos.y - pc.minHeight) / (pc.maxHeight - pc.minHeight)); 
    float heightGradient = smoothstep(0.f, 0.1f, h) * smoothstep(1.f, 0.8f, h);

    return saturate(n - pc.coverage) * heightGradient;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;
    uint2 outSize;
    outputImage.GetDimensions(outSize.x, outSize.y);
    if (pixel.x >= outSize.x || pixel.y >= outSize.y) return;

    float2 uv = (float2(pixel) + 0.5f) / float2(outSize);
    float2 ndc = uv * 2.f - 1.f;

    float4 nearPoint = mul(globalBuffer.Camera.InvViewProj, float4(ndc, 0.f, 1.f));
    float4 farPoint  = mul(globalBuffer.Camera.InvViewProj, float4(ndc, 1.f, 1.f));
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;

    float3 rayOrigin = globalBuffer.Camera.Pos;
    float3 rayDir = normalize(farPoint.xyz - nearPoint.xyz);

	uint2 depthSize;
	depthTexture.GetDimensions(depthSize.x, depthSize.y);
	uint2 depthCoord = uint2(uv * float2(depthSize));
	depthCoord = min(depthCoord, depthSize - 1u);

    float rawDepth = depthTexture.Load(int3(depthCoord, 0));
    float sceneDist;
    if (rawDepth >= 1.f)
	{
        sceneDist = 1e9f; // sky, no occlusion
    }
	else
	{
        float4 worldPos = mul(globalBuffer.Camera.InvViewProj, float4(ndc, rawDepth, 1.f));
        worldPos /= worldPos.w;
        sceneDist = length(worldPos.xyz - rayOrigin);
    }

    // Ray-slab intersection
    float tMinPlane = (pc.minHeight - rayOrigin.y) / rayDir.y;
    float tMaxPlane = (pc.maxHeight - rayOrigin.y) / rayDir.y;
    float tNear = min(tMinPlane, tMaxPlane);
    float tFar  = max(tMinPlane, tMaxPlane);

    tNear = max(tNear, 0.f);
    tFar = min(tFar, sceneDist);

    if (tFar <= tNear || rayDir.y == 0.f)
	{
        outputImage[pixel] = float4(0.f, 0.f, 0.f, 0.f);
        return;
    }

    float stepSize = (tFar - tNear) / float(pc.viewStepCount);
    float viewTransmittance = 1.f;
    float t = tNear;
	float3 scatteredLight = float3(0.f, 0.f, 0.f);
	const float3 sunDir = normalize(-globalBuffer.Lights.DirLights[0].Dir);
	const float3 sunColor = globalBuffer.Lights.DirLights[0].Color;
	const float sunIntensity = globalBuffer.Lights.DirLights[0].Intensity;
	const float extinction = 0.01f;
	const float cosTheta = dot(rayDir, sunDir);
	const float phase = HenyeyGreenstein(cosTheta, pc.anisotropy);
	const float3 ambientColor = float3(0.5f, 0.55f, 0.65f) * 0.3f;

	for (uint i = 0u; i < pc.viewStepCount; i++)
	{
        float3 samplePos = rayOrigin + rayDir * t;
        float density = SampleDensity(samplePos);

		float sunTNear, sunTFar;
		float sunTMinPlane = (pc.minHeight - samplePos.y) / sunDir.y;
		float sunTMaxPlane = (pc.maxHeight - samplePos.y) / sunDir.y;
		sunTNear = max(min(sunTMinPlane, sunTMaxPlane), 0.f);
		sunTFar = max(sunTMinPlane, sunTMaxPlane);

		float sunStepSize = (sunTFar - sunTNear) / float(pc.sunStepCount);
		float sunT = sunTNear;

        if (density > 0.f)
		{
			float sigmaT = density * extinction;
			float sunTransmittance = 1.f;

			for (uint j = 0u; j < pc.sunStepCount; j++)
			{
				float3 sunSamplePos = samplePos + sunDir * sunT;
				float sunDensity = SampleDensity(sunSamplePos);
	
				if (sunDensity > 0.f)
				{
					sunTransmittance *= exp(-sunDensity * sunStepSize * extinction);
				}
				sunT += sunStepSize;
			}
			
			float3 sampleLight = (sunColor * sunIntensity * sunTransmittance * sigmaT * phase) + (ambientColor * sigmaT);
			scatteredLight += sampleLight * viewTransmittance * stepSize;
            viewTransmittance *= exp(-sigmaT * stepSize);
        }

        if (viewTransmittance < 0.01f) break;
        t += stepSize;
    }

    outputImage[pixel] = float4(scatteredLight, 1.f - viewTransmittance);
}
