#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

namespace Hikari::Rhi::D3D12
{
/**
 * A heap of descriptors the GPU never reads through a table: render-target and
 * depth-stencil views, which a list binds by CPU handle. Separate from the
 * shader-visible heaps, which have rules of their own; these can be sized freely and
 * are not bound, so one fixed heap per kind with a free list is enough.
 *
 * Not synchronized: the device serializes access.
 */
class D3D12CpuDescriptorHeap
{
public:
    D3D12CpuDescriptorHeap(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                           const std::string& name);

    /** A free slot. Throws naming the heap and its capacity when there is none. */
    uint32_t Allocate(std::string_view kind);
    void Free(uint32_t index);

    D3D12_CPU_DESCRIPTOR_HANDLE HandleAt(uint32_t index) const;

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_Start{};
    uint32_t m_DescriptorSize = 0u;
    uint32_t m_Capacity = 0u;
    uint32_t m_Next = 0u;
    std::vector<uint32_t> m_Free;
};
} // namespace Hikari::Rhi::D3D12
