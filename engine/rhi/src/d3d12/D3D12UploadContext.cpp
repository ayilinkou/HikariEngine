#include "d3d12/D3D12UploadContext.h"

#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

#include <core/Log.h>

#include <rhi/BarrierPresets.h>
#include <rhi/ICommandList.h>
#include <rhi/Submit.h>

#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{
constexpr Core::LogCategory LogRhi("RHI");

namespace
{
/**
 * Where each subresource starts inside one staging buffer: aligned to 4 bytes, the
 * alignment Vulkan's copy requires of a buffer offset, so both backends lay staging
 * out identically.
 */
constexpr uint64_t kStagingCopyAlignment = 4u;

constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}
} // namespace

D3D12UploadContext::D3D12UploadContext(D3D12Device& device, const UploadContextDesc& desc)
    : m_Device(device), m_Desc(desc),
      m_Queue(device.ListTypeFor(QueueType::Copy) == D3D12_COMMAND_LIST_TYPE_COPY
                  ? QueueType::Copy
                  : QueueType::Graphics)
{
    const std::string name =
        m_Desc.DebugName.empty() ? std::string("Upload Context") : m_Desc.DebugName;

    m_Allocator = m_Device.CreateCommandAllocator(
        CommandAllocatorDesc{.Queue = m_Queue, .DebugName = name + " Allocator"});
    m_Fence = m_Device.CreateFence(FenceDesc{.InitialValue = 0u, .DebugName = name + " Fence"});

    Core::LogMsg(Core::LogSeverity::Info, LogRhi, "Upload context '{}' uploads on the {} queue.",
                 name, m_Queue == QueueType::Copy ? "copy" : "direct");
}

D3D12UploadContext::~D3D12UploadContext()
{
    if (!m_BufferCopies.empty() || !m_TextureCopies.empty())
    {
        Core::LogMsg(
            Core::LogSeverity::Error, LogRhi,
            "Upload context destroyed with {} buffer and {} texture upload(s) never flushed — "
            "those resources were never filled.",
            m_BufferCopies.size(), m_TextureCopies.size());
    }

    for (const BufferHandle staging : m_Staging)
        m_Device.Destroy(staging);

    m_Device.Destroy(m_Fence);

    Core::LogMsg(Core::LogSeverity::Info, LogRhi,
                 "Upload context destroyed after {} submission(s) for {} resource(s), {:.1f} MiB.",
                 m_Stats.Submits, m_Stats.Uploads,
                 static_cast<double>(m_Stats.Bytes) / (1024.0 * 1024.0));
}

BufferHandle D3D12UploadContext::CreateStaging(uint64_t size, const char* what)
{
    const BufferHandle staging = m_Device.CreateBuffer(
        BufferDesc{.Size = size,
                   .Usage = BufferUsage::CopySrc,
                   .Access = MemoryAccess::CpuToGpu,
                   .DebugName = std::format(
                       "{} Staging ({})",
                       m_Desc.DebugName.empty() ? std::string("Upload") : m_Desc.DebugName, what)});
    m_Staging.push_back(staging);
    return staging;
}

void D3D12UploadContext::FlushIfOverBudget(uint64_t bytes)
{
    const bool bPending = !m_BufferCopies.empty() || !m_TextureCopies.empty();
    if (bPending && m_PendingBytes + bytes > m_Desc.StagingBudget)
        Flush();
}

void D3D12UploadContext::UploadBuffer(BufferHandle destination, uint64_t destinationOffset,
                                      std::span<const std::byte> data)
{
    if (data.empty())
        return;

    FlushIfOverBudget(data.size_bytes());

    const BufferHandle staging = CreateStaging(data.size_bytes(), "buffer");
    std::memcpy(m_Device.GetMappedData(staging), data.data(), data.size_bytes());

    m_BufferCopies.push_back(PendingBufferCopy{.Staging = staging,
                                               .Destination = destination,
                                               .DestinationOffset = destinationOffset,
                                               .Size = data.size_bytes()});
    m_PendingBytes += data.size_bytes();
    ++m_Stats.Uploads;
    m_Stats.Bytes += data.size_bytes();
}

void D3D12UploadContext::UploadTexture(TextureHandle destination,
                                       std::span<const TextureUpload> subresources)
{
    uint64_t total = 0u;
    for (const TextureUpload& subresource : subresources)
        total = AlignUp(total, kStagingCopyAlignment) + subresource.Data.size_bytes();

    if (total == 0u)
        return;

    FlushIfOverBudget(total);

    const BufferHandle staging = CreateStaging(total, "texture");
    auto* pMapped = static_cast<std::byte*>(m_Device.GetMappedData(staging));

    PendingTextureCopy pending{.Staging = staging, .Destination = destination, .Subresources = {}};
    pending.Subresources.reserve(subresources.size());

    uint64_t offset = 0u;
    for (const TextureUpload& subresource : subresources)
    {
        offset = AlignUp(offset, kStagingCopyAlignment);
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

void D3D12UploadContext::Flush()
{
    if (m_BufferCopies.empty() && m_TextureCopies.empty())
        return;

    // The previous flush waited for its fence, so the GPU has finished with every list
    // this allocator handed out.
    m_Allocator->Reset();
    ICommandList& list = m_Allocator->Acquire();
    list.Begin();

    const bool bDirect = m_Queue != QueueType::Copy;

    std::vector<TextureBarrier> toCopyDst;
    std::vector<TextureBarrier> toShaderResource;
    if (bDirect)
    {
        for (const PendingTextureCopy& copy : m_TextureCopies)
        {
            const D3D12Texture* pTexture = m_Device.FindTexture(copy.Destination);
            if (pTexture == nullptr)
                continue; // Reported when the copy is recorded.

            const TextureDesc& desc = pTexture->Desc;
            const TextureAspect aspect = DefaultAspect(desc.Format);
            toCopyDst.push_back(
                BarrierPresets::UndefinedToCopyDst(desc.ArrayLayers, desc.MipLevels, aspect)
                    .On(copy.Destination));
            toShaderResource.push_back(
                BarrierPresets::CopyDstToShaderResource(desc.ArrayLayers, desc.MipLevels, aspect)
                    .On(copy.Destination));
        }

        list.Barrier(toCopyDst);
    }

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

    if (bDirect)
        list.Barrier(toShaderResource);

    list.End();

    ICommandList* lists[] = {&list};
    const FenceOperation signal{.Fence = m_Fence, .Value = ++m_FenceValue};
    m_Device.Submit(SubmitDesc{.Queue = m_Queue,
                               .CommandLists = lists,
                               .WaitFences = {},
                               .SignalFences = std::span<const FenceOperation>(&signal, 1),
                               .WaitSemaphores = {},
                               .SignalSemaphores = {}});
    ++m_Stats.Submits;

    m_Device.WaitForFence(m_Fence, m_FenceValue);

    const uint64_t flushedBytes = m_PendingBytes;
    const size_t flushedUploads = m_BufferCopies.size() + m_TextureCopies.size();

    for (const BufferHandle staging : m_Staging)
        m_Device.Destroy(staging);

    m_Staging.clear();
    m_BufferCopies.clear();
    m_TextureCopies.clear();
    m_PendingBytes = 0u;

    Core::LogMsg(Core::LogSeverity::Info, LogRhi,
                 "Upload flush: {} resource(s), {:.1f} MiB, in 1 submission.", flushedUploads,
                 static_cast<double>(flushedBytes) / (1024.0 * 1024.0));
}

} // namespace Hikari::Rhi::D3D12
