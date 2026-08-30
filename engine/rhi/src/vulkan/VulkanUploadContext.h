#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/Handles.h>
#include <rhi/TextureDesc.h>
#include <rhi/UploadContext.h>

#include "vulkan/OwnershipTransfer.h"

namespace Hikari::Rhi::Vulkan
{
class VulkanDevice;

/**
 * IUploadContext over its own command pool, command buffer and fence.
 *
 * The batch is built up in memory rather than recorded as it arrives, because
 * the barriers have to come first: a texture is transitioned to CopyDst before
 * any copy touches it and to ShaderResource after the last one, and both of
 * those cover every texture in the batch in a single command. Recording copies
 * eagerly would leave nowhere to put the opening barrier.
 *
 * Copies run on the copy queue when the device has one of its own. Handing the
 * results back to the graphics queue is then a queue family ownership transfer
 * — release on the copy queue, acquire on the graphics queue, ordered by a
 * semaphore between the two submissions — for those resources that need one.
 * Both halves happen inside Flush(), so the transfer is invisible to callers;
 * see Flush()'s definition for why the alternative, handing the acquire back
 * for someone else to record, was not taken.
 *
 * Which resources need one is a per-resource question under VK_KHR_maintenance9
 * and an unconditional yes without it, so a single batch can contain both kinds
 * and the acquire submission only happens when something in it was released.
 */
class VulkanUploadContext final : public IUploadContext
{
public:
    VulkanUploadContext(VulkanDevice& device, const UploadContextDesc& desc);
    ~VulkanUploadContext() override;

    void UploadBuffer(BufferHandle destination, uint64_t destinationOffset,
                      std::span<const std::byte> data) override;

    /**
     * Brings the interface's single-subresource shorthand along, which
     * overriding the span form would otherwise hide.
     */
    using IUploadContext::UploadTexture;

    void UploadTexture(TextureHandle destination,
                       std::span<const TextureUpload> subresources) override;

    void Flush() override;

    const UploadStats& GetStats() const override { return m_Stats; }

private:
    /** A staging buffer whose contents are already written, plus where they go. */
    struct PendingBufferCopy
    {
        BufferHandle Staging;
        BufferHandle Destination;
        uint64_t DestinationOffset = 0u;
        uint64_t Size = 0u;
    };

    /**
     * One texture, its staging buffer, and every subresource being written into
     * it. Held together because the barriers are per texture and the copies are
     * per subresource.
     */
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

    /**
     * Submits the pending batch if adding `bytes` to it would exceed the
     * budget. Called before recording rather than after, so that a batch never
     * ends up over budget and — more importantly — so that one call's data is
     * never split across two batches.
     */
    void FlushIfOverBudget(uint64_t bytes);

    /**
     * A CopySrc/CpuToGpu buffer holding a copy of `data`, tracked for release at
     * the next flush.
     */
    BufferHandle CreateStaging(uint64_t size, const char* what);

    /**
     * The release half of the ownership transfer, for those resources in the
     * pending batch that need one.
     *
     * The acquire half is these same barriers with the two scopes each
     * operation ignores swapped out, which is why they are built once and read
     * twice rather than described separately — the specification requires the
     * subresource range, the buffer range and both layouts to match exactly,
     * and matching is easiest to guarantee by construction.
     */
    struct OwnershipBarriers
    {
        std::vector<vk::ImageMemoryBarrier2> Images;
        std::vector<vk::BufferMemoryBarrier2> Buffers;

        bool Empty() const { return Images.empty() && Buffers.empty(); }
    };

    /**
     * The release barrier handing `image` over, or the transition that keeps it
     * on this queue. Both spell the same CopyDst -> ShaderResource move; only
     * the queue family indices and the scopes differ.
     */
    vk::ImageMemoryBarrier2 MakeReleaseBarrier(vk::Image image, const TextureDesc& desc) const;

    /** Turns the release barriers into their matching acquires, in place. */
    void MakeAcquireBarriers(OwnershipBarriers& barriers) const;

    /**
     * The dependency flags every ownership barrier is issued with. Empty
     * without VK_KHR_maintenance8, which is what pins those barriers' stage
     * masks to being ignored.
     */
    vk::DependencyFlags OwnershipDependencyFlags() const;

    VulkanDevice& m_Device;
    UploadContextDesc m_Desc;

    /**
     * Which family records and submits the copies, and which one owns the
     * results afterwards. Equal when the device has no separate copy family,
     * and then nothing can ever need transferring.
     */
    uint32_t m_CopyFamily = 0u;
    uint32_t m_GraphicsFamily = 0u;
    bool m_bSeparateCopyQueue = false;

    /**
     * What the device promises about handing resources on from m_CopyFamily,
     * and whether ownership barriers may name real pipeline stages
     * (VK_KHR_maintenance8) instead of being pinned to AllCommands.
     */
    OwnershipTransferRules m_TransferRules;
    bool m_bUseAllStages = false;

    vk::raii::CommandPool m_CopyPool = nullptr;
    vk::raii::CommandBuffer m_CopyCommandBuffer = nullptr;

    /**
     * The acquire half runs on the graphics queue, so it needs a pool of that
     * family: a command buffer may only be submitted to a queue of the family
     * its pool was created for. All three are null when the device has no
     * separate copy queue, and so can never have anything to hand over.
     */
    vk::raii::CommandPool m_AcquirePool = nullptr;
    vk::raii::CommandBuffer m_AcquireCommandBuffer = nullptr;
    vk::raii::Semaphore m_OwnershipSemaphore = nullptr;

    vk::raii::Fence m_Fence = nullptr;

    std::vector<PendingBufferCopy> m_BufferCopies;
    std::vector<PendingTextureCopy> m_TextureCopies;
    std::vector<BufferHandle> m_Staging;

    uint64_t m_PendingBytes = 0u;
    UploadStats m_Stats{};
};
} // namespace Hikari::Rhi::Vulkan
