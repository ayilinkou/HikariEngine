#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/Handles.h>
#include <rhi/UploadContext.h>

namespace Rhi::Vulkan
{
class VulkanDevice;

// IUploadContext over its own command pool, command buffer and fence.
//
// The batch is built up in memory rather than recorded as it arrives, because
// the barriers have to come first: a texture is transitioned to CopyDst before
// any copy touches it and to ShaderResource after the last one, and both of
// those cover every texture in the batch in a single command. Recording copies
// eagerly would leave nowhere to put the opening barrier.
class VulkanUploadContext final : public IUploadContext
{
public:
    VulkanUploadContext(VulkanDevice& device, const UploadContextDesc& desc);
    ~VulkanUploadContext() override;

    void UploadBuffer(BufferHandle destination, uint64_t destinationOffset,
                      std::span<const std::byte> data) override;

    // Brings the interface's single-subresource shorthand along, which
    // overriding the span form would otherwise hide.
    using IUploadContext::UploadTexture;

    void UploadTexture(TextureHandle destination,
                       std::span<const TextureUpload> subresources) override;

    void Flush() override;

    const UploadStats& GetStats() const override { return m_Stats; }

private:
    // A staging buffer whose contents are already written, plus where they go.
    struct PendingBufferCopy
    {
        BufferHandle Staging;
        BufferHandle Destination;
        uint64_t DestinationOffset = 0u;
        uint64_t Size = 0u;
    };

    // One texture, its staging buffer, and every subresource being written into
    // it. Held together because the barriers are per texture and the copies are
    // per subresource.
    struct PendingTextureCopy
    {
        BufferHandle Staging;
        TextureHandle Destination;

        struct Subresource
        {
            uint64_t StagingOffset = 0u;
            TextureAspect Aspect = TextureAspect::Color;
            uint32_t MipLevel = 0u;
            uint32_t BaseLayer = 0u;
            uint32_t LayerCount = 1u;
            Extent3D Extent{};
        };

        std::vector<Subresource> Subresources;
    };

    // Submits the pending batch if adding `bytes` to it would exceed the
    // budget. Called before recording rather than after, so that a batch never
    // ends up over budget and — more importantly — so that one call's data is
    // never split across two batches.
    void FlushIfOverBudget(uint64_t bytes);

    // A CopySrc/CpuToGpu buffer holding a copy of `data`, tracked for release at
    // the next flush.
    BufferHandle CreateStaging(uint64_t size, const char* what);

    VulkanDevice& m_Device;
    UploadContextDesc m_Desc;

    vk::raii::CommandPool m_CommandPool = nullptr;
    vk::raii::CommandBuffer m_CommandBuffer = nullptr;
    vk::raii::Fence m_Fence = nullptr;

    std::vector<PendingBufferCopy> m_BufferCopies;
    std::vector<PendingTextureCopy> m_TextureCopies;
    std::vector<BufferHandle> m_Staging;

    uint64_t m_PendingBytes = 0u;
    UploadStats m_Stats{};
};
} // namespace Rhi::Vulkan
