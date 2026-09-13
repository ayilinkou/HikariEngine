#pragma once

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
};

/** A sampler, held as its description for the same reason: it too is a descriptor. */
struct D3D12Sampler
{
    SamplerDesc Desc;
};
} // namespace Hikari::Rhi::D3D12
