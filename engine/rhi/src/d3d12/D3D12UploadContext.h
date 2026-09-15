#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <rhi/Handles.h>
#include <rhi/ICommandAllocator.h>
#include <rhi/Submit.h>
#include <rhi/UploadContext.h>

namespace Hikari::Rhi::D3D12
{
class D3D12Device;

/**
 * Stages uploads in host-visible buffers and copies them all in one submission per
 * flush, as Vulkan's does, so a scene's textures loaded inside one scope are the same
 * handful of submissions on either backend.
 *
 * The copies go to the copy queue with no barriers: a copy queue records none, and
 * everything it touches is in COMMON when it has run. Legacy barriers promote and decay
 * implicitly, so there a later transition to shader-resource may name that as its
 * before-state. Enhanced barriers do neither, so on that path the textures are then put
 * into shader-resource layout by a latch on the direct queue — a layout-only barrier
 * in a submission of its own, ordered after the copy. On a device behaving as a single
 * queue the copies are direct-queue work instead, where a write promotion does not
 * decay, so there the textures are transitioned explicitly — to copy destination, then
 * to shader resource. Either way an uploaded texture ends in the layout Vulkan leaves it
 * in.
 */
class D3D12UploadContext final : public IUploadContext
{
public:
    D3D12UploadContext(D3D12Device& device, const UploadContextDesc& desc);
    ~D3D12UploadContext() override;

    void UploadBuffer(BufferHandle destination, uint64_t destinationOffset,
                      std::span<const std::byte> data) override;
    void UploadTexture(TextureHandle destination,
                       std::span<const TextureUpload> subresources) override;
    using IUploadContext::UploadTexture;
    void Flush() override;
    const UploadStats& GetStats() const override { return m_Stats; }

private:
    struct PendingBufferCopy
    {
        BufferHandle Staging;
        BufferHandle Destination;
        uint64_t DestinationOffset = 0u;
        uint64_t Size = 0u;
    };

    struct PendingTextureCopy
    {
        struct Subresource
        {
            uint64_t StagingOffset = 0u;
            TextureAspect Aspect = TextureAspect::Color;
            uint32_t MipLevel = 0u;
            uint32_t BaseLayer = 0u;
            uint32_t LayerCount = 1u;
            Core::Extent3D Extent{};
        };

        BufferHandle Staging;
        TextureHandle Destination;
        std::vector<Subresource> Subresources;
    };

    BufferHandle CreateStaging(uint64_t size, const char* what);

    /** Submits the latch taking this flush's copied textures from COMMON to shader resource. */
    void LatchCopiedTextures(const FenceOperation& afterCopy);
    void FlushIfOverBudget(uint64_t bytes);

    D3D12Device& m_Device;
    UploadContextDesc m_Desc;
    QueueType m_Queue;

    std::unique_ptr<ICommandAllocator> m_Allocator;

    /** The direct-queue lists enhanced barriers latch copied textures with; null otherwise. */
    std::unique_ptr<ICommandAllocator> m_LatchAllocator;

    FenceHandle m_Fence;
    uint64_t m_FenceValue = 0u;

    std::vector<BufferHandle> m_Staging;
    std::vector<PendingBufferCopy> m_BufferCopies;
    std::vector<PendingTextureCopy> m_TextureCopies;
    uint64_t m_PendingBytes = 0u;
    UploadStats m_Stats;
};
} // namespace Hikari::Rhi::D3D12
