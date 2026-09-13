#pragma once

#include <cstdint>

#include <D3D12MemAlloc.h>
#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/BufferDesc.h>

namespace Hikari::Rhi::D3D12
{
/**
 * A buffer and the D3D12MA allocation that owns its memory. The allocation holds
 * the resource, so releasing it frees both; the separate reference to the
 * resource is only what callers reach for.
 */
struct D3D12Buffer
{
    Microsoft::WRL::ComPtr<D3D12MA::Allocation> Allocation;
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    BufferDesc Desc;

    /**
     * Mapped for the buffer's whole life when its heap is CPU-visible, null
     * otherwise — the contract IDevice::GetMappedData promises.
     */
    void* pMapped = nullptr;
};
} // namespace Hikari::Rhi::D3D12
