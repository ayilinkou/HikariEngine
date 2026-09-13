#include "d3d12/D3D12CommandList.h"

#include <array>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#include "d3d12/D3D12Conversions.h"
#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{

namespace
{
[[noreturn]] void ThrowNotImplemented(std::string_view method)
{
    throw std::logic_error(std::format("The D3D12 backend does not implement {} yet", method));
}

uint32_t LayersOf(const TextureDesc& desc)
{
    return desc.Dimension == TextureDimension::Texture3D ? 1u : desc.ArrayLayers;
}

/** Depth-stencil formats keep depth and stencil in two planes, each its own subresources. */
uint32_t PlanesOf(Format format)
{
    return format == Format::D24UnormS8Uint || format == Format::D32FloatS8Uint ? 2u : 1u;
}

/** D3D12CalcSubresource's arithmetic: mips within layers within planes. */
UINT Subresource(const TextureDesc& desc, uint32_t mip, uint32_t layer, uint32_t plane)
{
    return mip + layer * desc.MipLevels + plane * desc.MipLevels * LayersOf(desc);
}
} // namespace

D3D12CommandList::D3D12CommandList(D3D12Device& device, QueueType queue,
                                   D3D12_COMMAND_LIST_TYPE type)
    : m_Device(device), m_Queue(queue), m_Type(type)
{
    ID3D12Device& native = device.GetNativeDevice();

    HRESULT hr = native.CreateCommandAllocator(type, IID_PPV_ARGS(&m_Allocator));
    if (SUCCEEDED(hr))
        hr = native.CreateCommandList(0, type, m_Allocator.Get(), nullptr, IID_PPV_ARGS(&m_List));

    if (FAILED(hr))
    {
        device.DrainDebugMessages();
        throw std::runtime_error(std::format("Creating a D3D12 command list failed (0x{:08X})",
                                             static_cast<uint32_t>(hr)));
    }

    // A list is created open. Closing it here lets Begin reset it the same way on its
    // first use as on every later one.
    m_List->Close();
}

void D3D12CommandList::ResetAllocator()
{
    // Legal only once the GPU has finished what was recorded into it, which is the
    // same promise ICommandAllocator::Reset asks of its caller.
    const HRESULT hr = m_Allocator->Reset();
    m_Device.DrainDebugMessages();
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format(
            "Resetting a D3D12 command allocator failed (0x{:08X})", static_cast<uint32_t>(hr)));
    }
}

void D3D12CommandList::Begin()
{
    m_Transitions.clear();
    m_CopiedTextures.clear();

    const HRESULT hr = m_List->Reset(m_Allocator.Get(), nullptr);
    m_Device.DrainDebugMessages();
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format("Resetting a D3D12 command list failed (0x{:08X})",
                                             static_cast<uint32_t>(hr)));
    }
}

void D3D12CommandList::End()
{
    const HRESULT hr = m_List->Close();
    m_Device.DrainDebugMessages();
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format("Closing a D3D12 command list failed (0x{:08X}); the "
                                             "debug layer's messages above say why.",
                                             static_cast<uint32_t>(hr)));
    }
}

BarrierCounts D3D12CommandList::Barrier(std::span<const TextureBarrier> barriers)
{
    std::vector<D3D12_RESOURCE_BARRIER> converted;
    uint32_t counted = 0u;

    for (const TextureBarrier& barrier : barriers)
    {
        const D3D12Texture* pTexture = m_Device.FindTexture(barrier.Texture);
        if (pTexture == nullptr)
        {
            m_Device.ReportError(
                std::format("Rhi::ICommandList::Barrier: texture handle {:#010x} is stale or was "
                            "never valid; the barrier was not recorded.",
                            barrier.Texture.Value));
            continue;
        }

        // What the caller asked for is what is counted, whether or not D3D12 needs a
        // transition for it, so the counters stay one-to-one with Vulkan's.
        ++counted;

        const TextureDesc& desc = pTexture->Desc;
        const bool bWhole = barrier.BaseMip == 0u && barrier.MipCount == desc.MipLevels &&
                            barrier.BaseLayer == 0u && barrier.LayerCount == LayersOf(desc);

        // From Undefined means the contents are discarded, so any current state will
        // do — but a legacy transition has to name the real one. Earlier in this list
        // wins over what earlier submissions left.
        D3D12_RESOURCE_STATES before = ToLegacyState(barrier.OldLayout);
        if (barrier.OldLayout == TextureLayout::Undefined)
        {
            before = m_Device.SubmittedStateOf(barrier.Texture);
            for (const auto& [texture, state] : m_Transitions)
            {
                if (texture == barrier.Texture)
                    before = state;
            }
        }

        const D3D12_RESOURCE_STATES after = ToLegacyState(barrier.NewLayout);

        // D3D12 rejects a transition to the state a resource is already in, which
        // Vulkan allows; there is nothing to record.
        if (before != after)
        {
            D3D12_RESOURCE_BARRIER transition{};
            transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            transition.Transition.pResource = pTexture->Resource.Get();
            transition.Transition.StateBefore = before;
            transition.Transition.StateAfter = after;

            if (bWhole)
            {
                transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                converted.push_back(transition);
            }
            else
            {
                // A legacy barrier has no range: a partial one is one per subresource.
                for (uint32_t plane = 0u; plane < PlanesOf(desc.Format); ++plane)
                {
                    for (uint32_t layer = barrier.BaseLayer;
                         layer < barrier.BaseLayer + barrier.LayerCount; ++layer)
                    {
                        for (uint32_t mip = barrier.BaseMip;
                             mip < barrier.BaseMip + barrier.MipCount; ++mip)
                        {
                            transition.Transition.Subresource =
                                Subresource(desc, mip, layer, plane);
                            converted.push_back(transition);
                        }
                    }
                }
            }
        }

        // Only a whole-texture state can be the one an Undefined barrier resolves to.
        // A texture left partially transitioned has no one state to name, so a later
        // from-Undefined barrier on it would be wrong and the debug layer says so.
        if (bWhole)
            m_Transitions.emplace_back(barrier.Texture, after);
    }

    if (!converted.empty())
        m_List->ResourceBarrier(static_cast<UINT>(converted.size()), converted.data());

    m_Device.DrainDebugMessages();
    return BarrierCounts{.Barriers = counted, .Calls = counted > 0u ? 1u : 0u};
}

BarrierCounts D3D12CommandList::Barrier(const TextureBarrier& barrier)
{
    return Barrier(std::span<const TextureBarrier>(&barrier, 1));
}

void D3D12CommandList::CopyBuffer(BufferHandle source, BufferHandle destination,
                                  const BufferCopyRegion& region)
{
    ID3D12Resource* pSource = m_Device.FindBufferResource(source);
    ID3D12Resource* pDestination = m_Device.FindBufferResource(destination);
    if (pSource == nullptr || pDestination == nullptr)
    {
        m_Device.ReportError(std::format(
            "Rhi::ICommandList::CopyBuffer: buffer handle {:#010x} or {:#010x} is stale; the copy "
            "was not recorded.",
            source.Value, destination.Value));
        return;
    }

    m_List->CopyBufferRegion(pDestination, region.DstOffset, pSource, region.SrcOffset,
                             region.Size);
    m_Device.DrainDebugMessages();
}

void D3D12CommandList::CopyBufferToTexture(BufferHandle source, TextureHandle destination,
                                           const BufferTextureCopyRegion& region)
{
    CopyTextureLayers(destination, source, region, true);
}

void D3D12CommandList::CopyTextureToBuffer(TextureHandle source, BufferHandle destination,
                                           const BufferTextureCopyRegion& region)
{
    CopyTextureLayers(source, destination, region, false);
}

/**
 * The seam's region is Vulkan's: layers packed one after another in the buffer, each
 * tightly packed to its extent. D3D12 copies one subresource per call, so each layer
 * is its own copy at its own offset. The footprint's format comes from
 * GetCopyableFootprints, which knows a subresource's plane format; its pitch is
 * overridden to the tight one, which the unrestricted copy pitch the device was
 * required to have makes legal.
 */
void D3D12CommandList::CopyTextureLayers(TextureHandle texture, BufferHandle buffer,
                                         const BufferTextureCopyRegion& region, bool bToTexture)
{
    const D3D12Texture* pTexture = m_Device.FindTexture(texture);
    ID3D12Resource* pBuffer = m_Device.FindBufferResource(buffer);
    if (pTexture == nullptr || pBuffer == nullptr)
    {
        m_Device.ReportError(std::format(
            "Rhi::ICommandList::{}: texture handle {:#010x} or buffer handle {:#010x} is stale; "
            "the copy was not recorded.",
            bToTexture ? "CopyBufferToTexture" : "CopyTextureToBuffer", texture.Value,
            buffer.Value));
        return;
    }

    const TextureDesc& desc = pTexture->Desc;
    const uint64_t texelBytes = BytesPerTexel(desc.Format);
    const uint64_t layerBytes = static_cast<uint64_t>(region.Extent.Width) * region.Extent.Height *
                                region.Extent.Depth * texelBytes;
    const uint32_t plane = region.Aspect == TextureAspect::Stencil ? 1u : 0u;
    const D3D12_RESOURCE_DESC resourceDesc = pTexture->Resource->GetDesc();

    for (uint32_t layer = 0u; layer < region.LayerCount; ++layer)
    {
        const UINT subresource =
            Subresource(desc, region.MipLevel, region.BaseLayer + layer, plane);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        m_Device.GetNativeDevice().GetCopyableFootprints(&resourceDesc, subresource, 1, 0,
                                                         &footprint, nullptr, nullptr, nullptr);
        footprint.Offset = region.BufferOffset + layer * layerBytes;
        footprint.Footprint.Width = region.Extent.Width;
        footprint.Footprint.Height = region.Extent.Height;
        footprint.Footprint.Depth = region.Extent.Depth;
        footprint.Footprint.RowPitch = static_cast<UINT>(region.Extent.Width * texelBytes);

        D3D12_TEXTURE_COPY_LOCATION textureLocation{};
        textureLocation.pResource = pTexture->Resource.Get();
        textureLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        textureLocation.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION bufferLocation{};
        bufferLocation.pResource = pBuffer;
        bufferLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        bufferLocation.PlacedFootprint = footprint;

        if (bToTexture)
            m_List->CopyTextureRegion(&textureLocation, 0, 0, 0, &bufferLocation, nullptr);
        else
            m_List->CopyTextureRegion(&bufferLocation, 0, 0, 0, &textureLocation, nullptr);
    }

    if (m_Type == D3D12_COMMAND_LIST_TYPE_COPY)
        m_CopiedTextures.push_back(texture);

    m_Device.DrainDebugMessages();
}

/**
 * D3D12 has no rendering scope object: a scope is the targets bound to the output
 * merger, and a Clear load is an explicit clear restricted to the render area, as a
 * Vulkan clear load is. Preserve and Discard loads, and both store ops, record
 * nothing — D3D12 keeps a target's contents unless told otherwise, and discarding them
 * is a hint it does not need.
 */
void D3D12CommandList::BeginRendering(const RenderingDesc& desc)
{
    constexpr size_t kMaxRenderTargets = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    if (desc.RenderTargets.size() > kMaxRenderTargets)
        throw std::runtime_error("Rhi::ICommandList::BeginRendering: too many render targets.");

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxRenderTargets> renderTargets{};
    for (size_t i = 0; i < desc.RenderTargets.size(); ++i)
        renderTargets[i] = m_Device.RenderTargetViewFor(desc.RenderTargets[i].View);

    D3D12_CPU_DESCRIPTOR_HANDLE depthStencil{};
    if (desc.pDepthStencil != nullptr)
    {
        depthStencil =
            m_Device.DepthStencilViewFor(desc.pDepthStencil->View, desc.pDepthStencil->bReadOnly);
    }

    m_List->OMSetRenderTargets(static_cast<UINT>(desc.RenderTargets.size()), renderTargets.data(),
                               FALSE, desc.pDepthStencil ? &depthStencil : nullptr);

    const D3D12_RECT area{
        .left = desc.RenderArea.Offset.X,
        .top = desc.RenderArea.Offset.Y,
        .right = desc.RenderArea.Offset.X + static_cast<LONG>(desc.RenderArea.Extent.Width),
        .bottom = desc.RenderArea.Offset.Y + static_cast<LONG>(desc.RenderArea.Extent.Height)};

    // A clear over the whole target passes no rectangle. A rectangle that happens to
    // cover it does not count as initializing a target in memory D3D12MA did not
    // zero — the debug layer reports the first use of such a target as uninitialized
    // (measured) — whereas a whole clear does, as a Vulkan clear load over the full
    // area would.
    const auto clearRects = [this, &desc](TextureViewHandle view) -> UINT
    { return m_Device.RenderAreaCoversView(view, desc.RenderArea) ? 0u : 1u; };

    for (size_t i = 0; i < desc.RenderTargets.size(); ++i)
    {
        const RenderTarget& target = desc.RenderTargets[i];
        if (target.Load != LoadOp::Clear)
            continue;

        const UINT rects = clearRects(target.View);
        m_List->ClearRenderTargetView(renderTargets[i], target.ClearColor.data(), rects,
                                      rects ? &area : nullptr);
    }

    if (desc.pDepthStencil != nullptr && desc.pDepthStencil->Load == LoadOp::Clear &&
        !desc.pDepthStencil->bReadOnly)
    {
        const Format format = m_Device.TextureFormatOf(desc.pDepthStencil->View);
        const bool bStencil = format == Format::D24UnormS8Uint || format == Format::D32FloatS8Uint;
        const D3D12_CLEAR_FLAGS flags =
            bStencil ? D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL : D3D12_CLEAR_FLAG_DEPTH;

        const UINT rects = clearRects(desc.pDepthStencil->View);
        m_List->ClearDepthStencilView(depthStencil, flags, desc.pDepthStencil->ClearDepth,
                                      static_cast<UINT8>(desc.pDepthStencil->ClearStencil), rects,
                                      rects ? &area : nullptr);
    }

    m_Device.DrainDebugMessages();
}

// Nothing to end: the targets stay bound until the next scope binds others.
void D3D12CommandList::EndRendering() {}

void D3D12CommandList::SetPipeline(GraphicsPipelineHandle)
{
    ThrowNotImplemented("SetPipeline");
}

void D3D12CommandList::SetBindGroup(PipelineLayoutHandle, uint32_t, BindGroupHandle)
{
    ThrowNotImplemented("SetBindGroup");
}

void D3D12CommandList::SetPipeline(ComputePipelineHandle)
{
    ThrowNotImplemented("SetPipeline");
}

void D3D12CommandList::SetComputeBindGroup(PipelineLayoutHandle, uint32_t, BindGroupHandle)
{
    ThrowNotImplemented("SetComputeBindGroup");
}

void D3D12CommandList::PushConstants(PipelineLayoutHandle, ShaderStage, uint32_t,
                                     std::span<const std::byte>)
{
    ThrowNotImplemented("PushConstants");
}

void D3D12CommandList::Dispatch(uint32_t, uint32_t, uint32_t)
{
    ThrowNotImplemented("Dispatch");
}

void D3D12CommandList::SetVertexBuffer(uint32_t, BufferHandle, uint64_t)
{
    ThrowNotImplemented("SetVertexBuffer");
}

void D3D12CommandList::SetIndexBuffer(BufferHandle, IndexFormat, uint64_t)
{
    ThrowNotImplemented("SetIndexBuffer");
}

void D3D12CommandList::DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{
    ThrowNotImplemented("DrawIndexed");
}

void D3D12CommandList::SetViewport(const Viewport& viewport)
{
    const D3D12_VIEWPORT native{.TopLeftX = viewport.X,
                                .TopLeftY = viewport.Y,
                                .Width = viewport.Width,
                                .Height = viewport.Height,
                                .MinDepth = viewport.MinDepth,
                                .MaxDepth = viewport.MaxDepth};
    m_List->RSSetViewports(1, &native);
}

void D3D12CommandList::SetScissor(const Rect2D& rect)
{
    const D3D12_RECT native{.left = rect.Offset.X,
                            .top = rect.Offset.Y,
                            .right = rect.Offset.X + static_cast<LONG>(rect.Extent.Width),
                            .bottom = rect.Offset.Y + static_cast<LONG>(rect.Extent.Height)};
    m_List->RSSetScissorRects(1, &native);
}

} // namespace Hikari::Rhi::D3D12
