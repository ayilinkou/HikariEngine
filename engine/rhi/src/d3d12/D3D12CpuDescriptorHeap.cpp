#include "d3d12/D3D12CpuDescriptorHeap.h"

#include <format>
#include <stdexcept>

#include "d3d12/D3D12DebugName.h"

namespace Hikari::Rhi::D3D12
{

D3D12CpuDescriptorHeap::D3D12CpuDescriptorHeap(ID3D12Device& device,
                                               D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                                               const std::string& name)
    : m_Capacity(capacity)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = type;
    heapDesc.NumDescriptors = capacity;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    const HRESULT hr = device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_Heap));
    if (FAILED(hr))
    {
        throw std::runtime_error(
            std::format("CreateDescriptorHeap failed (0x{:08X})", static_cast<uint32_t>(hr)));
    }

    SetDebugName(*m_Heap.Get(), name);
    m_Start = m_Heap->GetCPUDescriptorHandleForHeapStart();

    // Queried rather than assumed: the size differs between adapters, and between a
    // device with the debug layer on and one without.
    m_DescriptorSize = device.GetDescriptorHandleIncrementSize(type);
}

uint32_t D3D12CpuDescriptorHeap::Allocate(std::string_view kind)
{
    if (!m_Free.empty())
    {
        const uint32_t index = m_Free.back();
        m_Free.pop_back();
        return index;
    }

    if (m_Next == m_Capacity)
    {
        throw std::runtime_error(std::format(
            "The D3D12 {} descriptor heap is full at {} descriptors: more views are rendering "
            "targets at once than it was sized for.",
            kind, m_Capacity));
    }

    return m_Next++;
}

void D3D12CpuDescriptorHeap::Free(uint32_t index)
{
    m_Free.push_back(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12CpuDescriptorHeap::HandleAt(uint32_t index) const
{
    return D3D12_CPU_DESCRIPTOR_HANDLE{m_Start.ptr + static_cast<SIZE_T>(index) * m_DescriptorSize};
}

} // namespace Hikari::Rhi::D3D12
