#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

namespace Hikari::Rhi::D3D12
{
/**
 * A fence, which D3D12 has natively: a monotonic counter a queue raises when it
 * reaches a Signal and a CPU or another queue waits on. The neutral fence was
 * modelled on it (plan D5), so nothing is emulated here.
 */
struct D3D12Fence
{
    Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
};
} // namespace Hikari::Rhi::D3D12
