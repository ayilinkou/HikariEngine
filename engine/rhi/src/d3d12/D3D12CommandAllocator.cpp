#include "d3d12/D3D12CommandAllocator.h"

#include <format>

#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{

D3D12CommandAllocator::D3D12CommandAllocator(D3D12Device& device, const CommandAllocatorDesc& desc,
                                             D3D12_COMMAND_LIST_TYPE type)
    : m_Device(device), m_DebugName(desc.DebugName), m_Queue(desc.Queue), m_Type(type)
{
}

ICommandList& D3D12CommandAllocator::Acquire()
{
    if (m_Acquired == m_Lists.size())
    {
        // Numbered as the Vulkan backend numbers its command buffers, so a list reads
        // the same in either backend's messages.
        std::string name;
        if (!m_DebugName.empty())
            name = std::format("{} [{}]", m_DebugName, m_Lists.size());

        m_Lists.push_back(std::make_unique<D3D12CommandList>(m_Device, m_Queue, m_Type, name));
    }

    return *m_Lists[m_Acquired++];
}

void D3D12CommandAllocator::Reset()
{
    for (size_t i = 0u; i < m_Acquired; ++i)
        m_Lists[i]->ResetAllocator();

    m_Acquired = 0u;
}

} // namespace Hikari::Rhi::D3D12
