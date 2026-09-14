#include "d3d12/D3D12GpuDescriptorHeap.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace Hikari::Rhi::D3D12
{

D3D12GpuDescriptorHeap::D3D12GpuDescriptorHeap(ID3D12Device& device,
                                               D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity,
                                               std::string capacityField, const wchar_t* name)
    : m_Capacity(capacity), m_CapacityField(std::move(capacityField))
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = type;
    heapDesc.NumDescriptors = capacity;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    const HRESULT hr = device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_Heap));
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format(
            "Creating a shader-visible descriptor heap of {} descriptors failed (0x{:08X}); "
            "DeviceDesc::{} may exceed what this adapter allows.",
            capacity, static_cast<uint32_t>(hr), m_CapacityField));
    }

    m_Heap->SetName(name);
    m_CpuStart = m_Heap->GetCPUDescriptorHandleForHeapStart();
    m_GpuStart = m_Heap->GetGPUDescriptorHandleForHeapStart();

    // Queried rather than assumed: the size differs between adapters, and with the
    // debug layer on.
    m_DescriptorSize = device.GetDescriptorHandleIncrementSize(type);

    m_Free.push_back(Range{.Start = 0u, .Count = capacity});
}

uint32_t D3D12GpuDescriptorHeap::Allocate(uint32_t count)
{
    for (auto it = m_Free.begin(); it != m_Free.end(); ++it)
    {
        if (it->Count < count)
            continue;

        const uint32_t start = it->Start;
        it->Start += count;
        it->Count -= count;
        if (it->Count == 0u)
            m_Free.erase(it);

        return start;
    }

    throw std::runtime_error(std::format(
        "A D3D12 descriptor heap has no {} free contiguous descriptors out of its {}. Raise "
        "DeviceDesc::{}: the heap is sized once and never grows, because every command list "
        "binds this one heap.",
        count, m_Capacity, m_CapacityField));
}

void D3D12GpuDescriptorHeap::Free(uint32_t start, uint32_t count)
{
    if (count == 0u)
        return;

    auto next = std::ranges::lower_bound(m_Free, start, {}, &Range::Start);
    next = m_Free.insert(next, Range{.Start = start, .Count = count});

    // Merge with the following range, then with the preceding one.
    if (auto after = std::next(next);
        after != m_Free.end() && next->Start + next->Count == after->Start)
    {
        next->Count += after->Count;
        m_Free.erase(after);
    }

    if (next != m_Free.begin())
    {
        auto before = std::prev(next);
        if (before->Start + before->Count == next->Start)
        {
            before->Count += next->Count;
            m_Free.erase(next);
        }
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12GpuDescriptorHeap::CpuHandle(uint32_t index) const
{
    return D3D12_CPU_DESCRIPTOR_HANDLE{m_CpuStart.ptr +
                                       static_cast<SIZE_T>(index) * m_DescriptorSize};
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12GpuDescriptorHeap::GpuHandle(uint32_t index) const
{
    return D3D12_GPU_DESCRIPTOR_HANDLE{m_GpuStart.ptr +
                                       static_cast<UINT64>(index) * m_DescriptorSize};
}

uint32_t D3D12GpuDescriptorHeap::IndexOf(D3D12_GPU_DESCRIPTOR_HANDLE handle) const
{
    const UINT64 offset = handle.ptr - m_GpuStart.ptr;
    if (handle.ptr < m_GpuStart.ptr || offset % m_DescriptorSize != 0u ||
        offset / m_DescriptorSize >= m_Capacity)
    {
        throw std::runtime_error(std::format(
            "A GPU descriptor handle ({:#x}) is not one of this heap's descriptors.", handle.ptr));
    }

    return static_cast<uint32_t>(offset / m_DescriptorSize);
}

} // namespace Hikari::Rhi::D3D12
