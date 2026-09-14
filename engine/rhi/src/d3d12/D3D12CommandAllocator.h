#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <directx/d3d12.h>

#include <rhi/ICommandAllocator.h>

#include "d3d12/D3D12CommandList.h"

namespace Hikari::Rhi::D3D12
{
class D3D12Device;

/**
 * Hands out lists for one queue type and recycles them all on Reset. Each list
 * carries its own native allocator (see D3D12CommandList), so this holds lists
 * rather than an ID3D12CommandAllocator of its own.
 */
class D3D12CommandAllocator final : public ICommandAllocator
{
public:
    D3D12CommandAllocator(D3D12Device& device, const CommandAllocatorDesc& desc,
                          D3D12_COMMAND_LIST_TYPE type);

    ICommandList& Acquire() override;
    void Reset() override;

private:
    D3D12Device& m_Device;
    std::string m_DebugName;
    QueueType m_Queue;
    D3D12_COMMAND_LIST_TYPE m_Type;

    std::vector<std::unique_ptr<D3D12CommandList>> m_Lists;
    size_t m_Acquired = 0u;
};
} // namespace Hikari::Rhi::D3D12
