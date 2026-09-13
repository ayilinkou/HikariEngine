#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/ICommandList.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Rhi::D3D12
{
class D3D12Device;

/**
 * One command list and the native allocator it records into.
 *
 * Its own allocator rather than a share of the neutral allocator's: D3D12 lets only
 * one list per native allocator record at a time, while the seam lets a caller hold
 * several lists from one allocator open at once, as Vulkan does. One native
 * allocator per list is what keeps that legal.
 *
 * Barriers take the legacy path: each TextureBarrier becomes a transition between
 * two D3D12_RESOURCE_STATES, its pipeline stages are discarded because legacy
 * barriers carry no synchronization scope, and a from-Undefined barrier resolves to
 * the state earlier submissions left the texture in. The states a list leaves its
 * textures in are recorded here and applied by the device at Submit.
 */
class D3D12CommandList final : public ICommandList
{
public:
    D3D12CommandList(D3D12Device& device, QueueType queue, D3D12_COMMAND_LIST_TYPE type);

    void Begin() override;
    void End() override;

    BarrierCounts Barrier(std::span<const TextureBarrier> barriers) override;
    BarrierCounts Barrier(const TextureBarrier& barrier) override;

    void BeginRendering(const RenderingDesc& desc) override;
    void EndRendering() override;
    void SetPipeline(GraphicsPipelineHandle pipeline) override;
    void SetBindGroup(PipelineLayoutHandle layout, uint32_t slot, BindGroupHandle group) override;
    void SetPipeline(ComputePipelineHandle pipeline) override;
    void SetComputeBindGroup(PipelineLayoutHandle layout, uint32_t slot,
                             BindGroupHandle group) override;
    void PushConstants(PipelineLayoutHandle layout, ShaderStage stages, uint32_t offset,
                       std::span<const std::byte> data) override;
    void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;
    void SetVertexBuffer(uint32_t slot, BufferHandle buffer, uint64_t offset) override;
    void SetIndexBuffer(BufferHandle buffer, IndexFormat format, uint64_t offset) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                     int32_t vertexOffset, uint32_t firstInstance) override;
    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const Rect2D& rect) override;

    void CopyBuffer(BufferHandle source, BufferHandle destination,
                    const BufferCopyRegion& region) override;
    void CopyBufferToTexture(BufferHandle source, TextureHandle destination,
                             const BufferTextureCopyRegion& region) override;
    void CopyTextureToBuffer(TextureHandle source, BufferHandle destination,
                             const BufferTextureCopyRegion& region) override;

    /** Makes the list recordable again; its allocator's memory is reused. */
    void ResetAllocator();

    QueueType Queue() const { return m_Queue; }
    ID3D12CommandList* Native() const { return m_List.Get(); }
    D3D12_COMMAND_LIST_TYPE Type() const { return m_Type; }

    /** The whole-texture states this list leaves behind, in recording order. */
    const std::vector<std::pair<TextureHandle, D3D12_RESOURCE_STATES>>& Transitions() const
    {
        return m_Transitions;
    }

    /** Textures a copy list touched, which decay to COMMON once it has executed. */
    const std::vector<TextureHandle>& CopiedTextures() const { return m_CopiedTextures; }

private:
    void CopyTextureLayers(TextureHandle texture, BufferHandle buffer,
                           const BufferTextureCopyRegion& region, bool bToTexture);

    D3D12Device& m_Device;
    QueueType m_Queue;
    D3D12_COMMAND_LIST_TYPE m_Type;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_Allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_List;

    std::vector<std::pair<TextureHandle, D3D12_RESOURCE_STATES>> m_Transitions;
    std::vector<TextureHandle> m_CopiedTextures;
};
} // namespace Hikari::Rhi::D3D12
