#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/Handles.h>
#include <rhi/PipelineCache.h>

namespace Hikari::Rhi::D3D12
{
/** A shader stage's DXIL, held until a pipeline state object consumes it. */
struct D3D12ShaderModule
{
    std::vector<std::byte> Bytes;
};

/**
 * A pipeline state object and what binding it needs from the list: its layout, whose
 * root signature the list sets, and each vertex buffer slot's stride, which D3D12
 * takes when a buffer is bound rather than when the pipeline is made.
 */
struct D3D12GraphicsPipeline
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> State;
    PipelineLayoutHandle Layout;
    std::array<uint32_t, D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> Strides{};
};

struct D3D12ComputePipeline
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> State;
    PipelineLayoutHandle Layout;
};

/**
 * No cache: Save writes nothing. On real hardware the driver already caches compiled
 * shaders across runs — the RX 580's reports an automatic disk cache — and the
 * ID3D12PipelineLibrary that would do better needs a stable hash of every pipeline
 * description and handling for a library the driver no longer accepts, which six
 * pipelines did not justify. Pipeline creation takes this and ignores it.
 */
class D3D12PipelineCache final : public IPipelineCache
{
public:
    bool Save() override { return false; }
};
} // namespace Hikari::Rhi::D3D12
