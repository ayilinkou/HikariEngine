#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

namespace Hikari::Rhi::D3D12
{
/**
 * One of the two shader-visible heaps every command list binds: resources or
 * samplers. Created once with the device at a fixed capacity and never replaced,
 * because at most one heap of each kind can be bound at a time, switching can stall,
 * and ImGui's DX12 backend keeps raw GPU descriptor handles into it.
 *
 * Bind groups take contiguous ranges, since a descriptor table is a contiguous run
 * of one heap. First fit over a free list that merges neighbours on release: the
 * ranges are small and few kinds of group exist, so fragmentation stays shallow.
 *
 * Not synchronized: the device serializes access.
 */
class D3D12GpuDescriptorHeap
{
public:
    /**
     * `capacityField` names the DeviceDesc field that sets the capacity, so an
     * exhausted heap says which number to raise.
     */
    D3D12GpuDescriptorHeap(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                           std::string capacityField, const wchar_t* name);

    /** The first index of `count` free contiguous descriptors. Throws when there are none. */
    uint32_t Allocate(uint32_t count);
    void Free(uint32_t start, uint32_t count);

    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const;
    ID3D12DescriptorHeap* Native() const { return m_Heap.Get(); }

private:
    struct Range
    {
        uint32_t Start = 0u;
        uint32_t Count = 0u;
    };

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_CpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_GpuStart{};
    uint32_t m_DescriptorSize = 0u;
    uint32_t m_Capacity = 0u;
    std::string m_CapacityField;

    /** Sorted by Start and never adjacent to one another, since Free merges neighbours. */
    std::vector<Range> m_Free;
};
} // namespace Hikari::Rhi::D3D12
