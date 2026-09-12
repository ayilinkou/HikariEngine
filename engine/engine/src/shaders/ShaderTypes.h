#pragma once

/*
 * The GPU constant blocks, declared once for C++ and once for nobody.
 *
 * These structs used to be written twice — here in HLSL and again in the
 * engine's C++ — with nothing linking the two. Changing one without the other
 * produced silent corruption rather than a build failure, which is the worst
 * shape a mistake can take.
 *
 * **HLSL sets the vocabulary, and C++ follows.** The aliases below are
 * transparent in the direction that matters: a `float4` *is* a `glm::vec4`, so
 * the C++ side keeps every glm operation it had. Going the other way is not
 * possible at all — Slang has no `glm::vec4` to alias. D13's D3D12-first naming
 * points the same way.
 *
 * **Scope is the constant and push-constant blocks only.** Vertex input is not
 * here and cannot be: `VS_In` carries semantics (`float3 Pos : POSITION0`) that
 * C++ has no way to express, so the two declarations stay separate and a test
 * compares them instead (plan D32).
 *
 * Two rules for anything added here:
 *
 * **`bool32` rather than `bool`.** A C++ `bool` is one byte and a shader's is
 * four, so a shared block containing one would disagree about every offset after
 * it. `bool32` is a real `bool` to the shader — it tests and combines like one,
 * with no `!= 0` at the use site — and a 32-bit integer to C++, so both sides
 * lay it out the same. `bool` itself cannot be aliased: it is a C++ keyword, and
 * a macro over it would follow this header into every translation unit that
 * includes it.
 *
 * The 32-bit shader side is a measurement rather than an assumption, and it is
 * re-measured on every build: ShaderLayoutTests compares this block's reflected
 * sizes against `sizeof` on both targets, so the day a compiler lays a `bool`
 * out differently is a failing test rather than a corrupt material.
 *
 * **Matrices are uploaded transposed.** `glm::mat4` and `float4x4` are both 64
 * bytes, so the layout agrees; what makes them mean the same thing is the
 * convention around them. The engine transposes on upload
 * (`Engine.cpp`'s `UpdateGlobalBuffer`) and the shaders multiply row-vector
 * first — `mul(worldPos, View)` rather than `mul(View, worldPos)`. Change one
 * half and the image inverts in a way no offset check would catch.
 */

#ifdef __cplusplus
#include <cstdint>

#include <glm/glm.hpp>

// Slang's preprocessor skips this whole block, so the includes above never
// reach a shader compile. Verified on both targets.
//
// uint32_t rather than an alias for `uint`: Slang accepts uint32_t, and glibc
// already declares `uint` under <sys/types.h>, so defining it here would be a
// redeclaration that happens to agree on this platform and need not on another.
/**
 * A boolean as a GPU block carries one: four bytes, which is what a shader's
 * `bool` occupies and what C++'s does not.
 */
using bool32 = int32_t;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
using float4x4 = glm::mat4;
#endif

#ifndef __cplusplus
/**
 * The shader half of the alias above: a real bool, so `if (pc.bTwoSided)` reads
 * as it should, and four bytes, which is what a shader's bool already occupies.
 */
typedef bool bool32;
#endif

#include "../Common.h"

struct PointLightData
{
    float3 Color;
    float Intensity;
    float3 Pos;
    float Padding;
};

struct DirLightData
{
    float3 Color;
    float Intensity;
    float3 Dir;
    float Padding;
};

struct LightData
{
    uint32_t PointLightCount;
    uint32_t DirLightCount;
    float2 Padding;
    PointLightData PointLights[MAX_POINT_LIGHTS];
    DirLightData DirLights[MAX_DIR_LIGHTS];
};

/*
 * Each member starts at an offset that is a multiple of its base alignment, so
 * a float can start at 0, 4, 8 or 12 — and a float3 is twelve bytes wide but
 * sixteen-byte aligned, which is what every Padding member here exists for.
 */
struct CameraData
{
    float4x4 View;
    float4x4 Proj;
    float4x4 InvViewProj;
    float3 Pos;
    float NearPlane;
    float3 Padding;
    float FarPlane;
};

struct GlobalBuffer
{
    LightData Lights;
    CameraData Camera;
    float3 SkyColor;
    float Time;
};

struct MaterialPushConstant
{
    float4 Albedo = float4(1.f, 0.f, 1.f, 1.f);
    float Metallic = 0.f;
    float Roughness = 1.f;
    float AO = 1.f;
    float Opacity = 1.f;

    bool32 bHasAlbedoTex = false;
    bool32 bHasNormalTex = false;
    bool32 bHasMetallicRoughnessTex = false;
    bool32 bTwoSided = false;
};

struct CloudPushConstants
{
    float3 WindVelocity = float3(0.05f, 0.f, 0.03f);
    float MinHeight = 1500.f;
    float MaxHeight = 4000.f;
    float Coverage = 0.2f;
    float Anisotropy = 0.3f;
    float BoundaryDisplacement = 300.f;
    uint32_t ViewStepCount = 64;
    uint32_t SunStepCount = 6;
};

struct BakeConstants
{
    uint32_t Resolution;
    uint32_t WorleyPointsPerCell;
};
