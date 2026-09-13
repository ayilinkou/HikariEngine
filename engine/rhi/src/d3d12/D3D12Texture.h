#pragma once

#include <cstdint>
#include <optional>

#include <D3D12MemAlloc.h>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>

namespace Hikari::Rhi::D3D12
{
/** A texture and the D3D12MA allocation that owns its memory. */
struct D3D12Texture
{
    Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    TextureDesc Desc;

    /**
     * The state the command lists submitted so far leave the whole texture in, which
     * is what a legacy barrier from TextureLayout::Undefined resolves to. Advanced at
     * submission rather than at recording, so a recorder on one thread reads what
     * every earlier submission left rather than what another thread has recorded
     * but not yet submitted. Guarded by the device's state mutex.
     */
    D3D12_RESOURCE_STATES SubmittedState = D3D12_RESOURCE_STATE_COMMON;
};

/**
 * A view, held as its description. D3D12 has no view objects — a view is a
 * descriptor written into a heap — and the heaps are persistent and filled when a
 * bind group, a render target or a depth-stencil target is made from the view, so
 * until then there is nothing to create.
 */
struct D3D12TextureView
{
    TextureViewDesc Desc;

    /**
     * Slots in the device's render-target and depth-stencil heaps, written the first
     * time the view is a rendering target and freed with the view. A read-only depth
     * view is a different descriptor from a writable one, so it has its own slot.
     */
    std::optional<uint32_t> RenderTargetSlot;
    std::optional<uint32_t> DepthStencilSlot;
    std::optional<uint32_t> ReadOnlyDepthStencilSlot;
};

/** A sampler, held as its description for the same reason: it too is a descriptor. */
struct D3D12Sampler
{
    SamplerDesc Desc;
};
} // namespace Hikari::Rhi::D3D12
