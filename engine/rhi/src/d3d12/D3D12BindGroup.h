#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/BindGroup.h>
#include <rhi/Handles.h>
#include <rhi/Pipeline.h>

namespace Hikari::Rhi::D3D12
{
/**
 * A layout's bindings split the way D3D12 stores them: resources — constant
 * buffers, textures and unordered-access textures — in one table, and samplers in
 * another, since the two live in different heaps. Each binding's offset is its place
 * within its table.
 */
struct D3D12BindGroupLayout
{
    struct Entry
    {
        BindGroupLayoutBinding Binding;
        uint32_t Offset = 0u;
    };

    std::vector<Entry> Entries;
    uint32_t ResourceCount = 0u;
    uint32_t SamplerCount = 0u;

    /** The union of the stages that read any resource, or any sampler. */
    ShaderStage ResourceVisibility = ShaderStage::None;
    ShaderStage SamplerVisibility = ShaderStage::None;
};

/**
 * A bind group's ranges in the two heaps. The sampler range may be shared with other
 * groups whose samplers are identical, which is what keeps a scene of many materials
 * inside the 2,048 samplers D3D12 guarantees.
 */
struct D3D12BindGroup
{
    BindGroupLayoutHandle Layout;
    uint32_t ResourceStart = 0u;
    uint32_t ResourceCount = 0u;

    /** Index into the device's shared sampler ranges; empty when the layout has no samplers. */
    std::optional<size_t> SamplerRange;
};

/**
 * A pipeline layout's root signature, and where in it each bind group's tables and the
 * push constants landed.
 */
struct D3D12PipelineLayout
{
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;

    /** Root parameter index of group N's resource and sampler tables, when it has them. */
    std::vector<std::optional<UINT>> ResourceTableParameters;
    std::vector<std::optional<UINT>> SamplerTableParameters;

    std::optional<UINT> PushConstantParameter;
};
} // namespace Hikari::Rhi::D3D12
