#include "vulkan/VulkanUploadContext.h"

#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

#include <core/Log.h>

#include <rhi/BarrierPresets.h>
#include <rhi/ICommandList.h>
#include <rhi/vulkan/DebugNames.h>

#include "vulkan/VulkanCommandList.h"
#include "vulkan/VulkanDevice.h"

namespace Rhi::Vulkan
{
namespace
{
constexpr LogCategory LogRhi("RHI");

// Staging is written by the CPU and read once by the copy, which is exactly what
// CpuToGpu describes.
constexpr MemoryAccess kStagingAccess = MemoryAccess::CpuToGpu;
} // namespace

VulkanUploadContext::VulkanUploadContext(VulkanDevice& device, const UploadContextDesc& desc)
    : m_Device(device), m_Desc(desc)
{
    const std::string name =
        m_Desc.DebugName.empty() ? std::string("Upload Context") : m_Desc.DebugName;

    vk::raii::Device& vkDevice = m_Device.GetDevice();

    // Transient because every buffer this pool hands out is recorded, submitted
    // and reset within one flush.
    //
    // The graphics family, not the copy one, because a command buffer may only
    // be submitted to a queue of the family its pool was created for and the
    // graphics queue is the only one this device actually creates. Moving
    // uploads to a dedicated transfer queue changes the pool and the submit
    // together, and brings a queue-family ownership transfer with it.
    const vk::CommandPoolCreateInfo poolInfo{.flags = vk::CommandPoolCreateFlagBits::eTransient,
                                             .queueFamilyIndex =
                                                 m_Device.GetQueueFamily(QueueType::Graphics)};
    m_CommandPool = vk::raii::CommandPool(vkDevice, poolInfo);
    SetVkDebugName(vkDevice, *m_CommandPool, vk::ObjectType::eCommandPool,
                   (name + " Command Pool").c_str());

    const vk::CommandBufferAllocateInfo allocInfo{.commandPool = *m_CommandPool,
                                                  .level = vk::CommandBufferLevel::ePrimary,
                                                  .commandBufferCount = 1u};
    m_CommandBuffer = std::move(vk::raii::CommandBuffers(vkDevice, allocInfo).front());
    SetVkDebugName(vkDevice, *m_CommandBuffer, vk::ObjectType::eCommandBuffer,
                   (name + " Command Buffer").c_str());

    // Created unsignaled: the first thing done with it is a submit, and the wait
    // that follows must not return before that submit completes.
    m_Fence = vk::raii::Fence(vkDevice, vk::FenceCreateInfo{});
    SetVkDebugName(vkDevice, *m_Fence, vk::ObjectType::eFence, (name + " Fence").c_str());
}

VulkanUploadContext::~VulkanUploadContext()
{
    // Anything still pending was recorded and never flushed, which means the
    // resources it was meant to fill are holding uninitialised memory. Doing the
    // upload now would be too late to help — whoever owns those resources has
    // already been handed them — so this reports rather than papers over it, and
    // releases the staging so the device does not also report leaked buffers.
    if (!m_BufferCopies.empty() || !m_TextureCopies.empty())
    {
        LogMsg(LogSeverity::Error, LogRhi,
               "Upload context destroyed with {} buffer and {} texture upload(s) never flushed — "
               "those resources were never filled.",
               m_BufferCopies.size(), m_TextureCopies.size());
    }

    for (const BufferHandle staging : m_Staging)
        m_Device.Destroy(staging);

    // The one line that says what the batching actually bought, which is
    // otherwise only visible by counting flush lines in the log.
    LogMsg(LogSeverity::Info, LogRhi,
           "Upload context destroyed after {} submission(s) for {} resource(s), {:.1f} MiB.",
           m_Stats.Submits, m_Stats.Uploads,
           static_cast<double>(m_Stats.Bytes) / (1024.0 * 1024.0));
}

BufferHandle VulkanUploadContext::CreateStaging(uint64_t size, const char* what)
{
    const BufferHandle staging = m_Device.CreateBuffer(
        BufferDesc{.Size = size,
                   .Usage = BufferUsage::CopySrc,
                   .Access = kStagingAccess,
                   .DebugName = std::format(
                       "{} Staging ({})",
                       m_Desc.DebugName.empty() ? std::string("Upload") : m_Desc.DebugName, what)});
    m_Staging.push_back(staging);
    return staging;
}

void VulkanUploadContext::FlushIfOverBudget(uint64_t bytes)
{
    const bool bPending = !m_BufferCopies.empty() || !m_TextureCopies.empty();
    if (bPending && m_PendingBytes + bytes > m_Desc.StagingBudget)
        Flush();
}

void VulkanUploadContext::UploadBuffer(BufferHandle destination, uint64_t destinationOffset,
                                       std::span<const std::byte> data)
{
    if (data.empty())
        return;

    FlushIfOverBudget(data.size_bytes());

    const BufferHandle staging = CreateStaging(data.size_bytes(), "buffer");

    void* pMapped = m_Device.GetMappedData(staging);
    if (pMapped == nullptr)
        throw std::runtime_error("Rhi::IUploadContext::UploadBuffer: staging is not host-visible.");

    std::memcpy(pMapped, data.data(), data.size_bytes());

    m_BufferCopies.push_back(PendingBufferCopy{.Staging = staging,
                                               .Destination = destination,
                                               .DestinationOffset = destinationOffset,
                                               .Size = data.size_bytes()});
    m_PendingBytes += data.size_bytes();
    ++m_Stats.Uploads;
    m_Stats.Bytes += data.size_bytes();
}

void VulkanUploadContext::UploadTexture(TextureHandle destination,
                                        std::span<const TextureUpload> subresources)
{
    if (subresources.empty())
        return;

    uint64_t total = 0u;
    for (const TextureUpload& subresource : subresources)
        total += subresource.Data.size_bytes();

    if (total == 0u)
        return;

    // Whole texture or nothing: see IUploadContext::UploadTexture for why
    // splitting one across two batches would discard the first batch's pixels.
    FlushIfOverBudget(total);

    const BufferHandle staging = CreateStaging(total, "texture");

    auto* pMapped = static_cast<std::byte*>(m_Device.GetMappedData(staging));
    if (pMapped == nullptr)
    {
        throw std::runtime_error(
            "Rhi::IUploadContext::UploadTexture: staging is not host-visible.");
    }

    PendingTextureCopy pending{.Staging = staging, .Destination = destination, .Subresources = {}};
    pending.Subresources.reserve(subresources.size());

    uint64_t offset = 0u;
    for (const TextureUpload& subresource : subresources)
    {
        std::memcpy(pMapped + offset, subresource.Data.data(), subresource.Data.size_bytes());

        pending.Subresources.push_back(
            PendingTextureCopy::Subresource{.StagingOffset = offset,
                                            .Aspect = subresource.Aspect,
                                            .MipLevel = subresource.MipLevel,
                                            .BaseLayer = subresource.BaseLayer,
                                            .LayerCount = subresource.LayerCount,
                                            .Extent = subresource.Extent});

        offset += subresource.Data.size_bytes();
    }

    m_TextureCopies.push_back(std::move(pending));
    m_PendingBytes += total;
    ++m_Stats.Uploads;
    m_Stats.Bytes += total;
}

void VulkanUploadContext::Flush()
{
    if (m_BufferCopies.empty() && m_TextureCopies.empty())
        return;

    // Safe because every buffer from this pool was submitted and waited on by
    // the previous flush.
    m_CommandPool.reset();

    VulkanCommandList list(m_Device, *m_CommandBuffer);
    list.Begin();

    // Both barrier batches cover the whole of each texture rather than only the
    // subresources being written. A layout is a property of a subresource, and
    // leaving the untouched ones in Undefined would make them illegal to sample
    // through a view that spans the whole resource — which is the only kind of
    // view anything here creates.
    std::vector<TextureBarrier> toCopyDst;
    std::vector<TextureBarrier> toShaderResource;
    toCopyDst.reserve(m_TextureCopies.size());
    toShaderResource.reserve(m_TextureCopies.size());

    for (const PendingTextureCopy& copy : m_TextureCopies)
    {
        const TextureDesc* pDesc = m_Device.GetTextureDesc(copy.Destination);
        if (pDesc == nullptr)
            continue; // Reported by the command list when the copy is recorded.

        const uint32_t mips = pDesc->MipLevels;
        const uint32_t layers = pDesc->ArrayLayers;
        const TextureAspect aspect = DefaultAspect(pDesc->Format);

        toCopyDst.push_back(
            BarrierPresets::UndefinedToCopyDst(layers, mips, aspect).On(copy.Destination));
        toShaderResource.push_back(
            BarrierPresets::CopyDstToShaderResource(layers, mips, aspect).On(copy.Destination));
    }

    list.Barrier(toCopyDst);

    for (const PendingBufferCopy& copy : m_BufferCopies)
    {
        list.CopyBuffer(copy.Staging, copy.Destination,
                        BufferCopyRegion{.SrcOffset = 0u,
                                         .DstOffset = copy.DestinationOffset,
                                         .Size = copy.Size});
    }

    for (const PendingTextureCopy& copy : m_TextureCopies)
    {
        for (const PendingTextureCopy::Subresource& subresource : copy.Subresources)
        {
            list.CopyBufferToTexture(
                copy.Staging, copy.Destination,
                BufferTextureCopyRegion{.BufferOffset = subresource.StagingOffset,
                                        .Aspect = subresource.Aspect,
                                        .MipLevel = subresource.MipLevel,
                                        .BaseLayer = subresource.BaseLayer,
                                        .LayerCount = subresource.LayerCount,
                                        .Extent = subresource.Extent});
        }
    }

    list.Barrier(toShaderResource);
    list.End();

    const vk::CommandBuffer commandBuffer = *m_CommandBuffer;
    const vk::SubmitInfo submitInfo{.commandBufferCount = 1u, .pCommandBuffers = &commandBuffer};
    m_Device.GetGraphicsQueue().submit(submitInfo, *m_Fence);

    // No barrier is needed between these copies and whatever reads the results
    // in a later submission, and that is a specification guarantee rather than
    // an assumption. A fence signal's first access scope is "all memory access
    // performed by the device", so waiting on it makes these writes available;
    // the next queue submission's second access scope is likewise all device
    // access, which makes them visible to everything it contains (Vulkan 1.4,
    // *Fences* and *Host Write Ordering Guarantees*). The queue drain this
    // replaced was defined as exactly this fence wait, so nothing is given up.
    const vk::Result result = m_Device.GetDevice().waitForFences(
        *m_Fence, vk::True, std::numeric_limits<uint64_t>::max());
    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error(
            std::format("Rhi::IUploadContext::Flush: waiting on the upload fence failed: {}.",
                        vk::to_string(result)));
    }
    m_Device.GetDevice().resetFences(*m_Fence);

    const uint64_t flushedBytes = m_PendingBytes;
    const size_t flushedUploads = m_BufferCopies.size() + m_TextureCopies.size();

    for (const BufferHandle staging : m_Staging)
        m_Device.Destroy(staging);

    m_Staging.clear();
    m_BufferCopies.clear();
    m_TextureCopies.clear();
    m_PendingBytes = 0u;
    ++m_Stats.Submits;

    LogMsg(LogSeverity::Info, LogRhi, "Upload flush: {} resource(s), {:.1f} MiB, in 1 submission.",
           flushedUploads, static_cast<double>(flushedBytes) / (1024.0 * 1024.0));
}
} // namespace Rhi::Vulkan
