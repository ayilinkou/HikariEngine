#include "d3d12/D3D12CommandAllocator.h"

#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{

D3D12CommandAllocator::D3D12CommandAllocator(D3D12Device& device, const CommandAllocatorDesc& desc,
                                             D3D12_COMMAND_LIST_TYPE type)
    : m_Device(device), m_Queue(desc.Queue), m_Type(type)
{
}

ICommandList& D3D12CommandAllocator::Acquire()
{
    if (m_Acquired == m_Lists.size())
        m_Lists.push_back(std::make_unique<D3D12CommandList>(m_Device, m_Queue, m_Type));

    return *m_Lists[m_Acquired++];
}

void D3D12CommandAllocator::Reset()
{
    for (size_t i = 0u; i < m_Acquired; ++i)
        m_Lists[i]->ResetAllocator();

    m_Acquired = 0u;
}

} // namespace Hikari::Rhi::D3D12
