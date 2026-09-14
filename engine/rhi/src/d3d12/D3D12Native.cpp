#include <rhi/d3d12/D3D12Native.h>

#include <stdexcept>

#include "d3d12/D3D12CommandList.h"
#include "d3d12/D3D12Conversions.h"
#include "d3d12/D3D12Device.h"

namespace Hikari::Rhi::D3D12
{
namespace
{
/**
 * A dynamic_cast, as the Vulkan accessors use: handing these another backend's device
 * is undefined behaviour under a static_cast, and they run a handful of times at
 * startup, never per frame.
 */
D3D12Device& AsD3D12(IDevice& device)
{
    auto* pDevice = dynamic_cast<D3D12Device*>(&device);
    if (pDevice == nullptr)
        throw std::runtime_error("Rhi::D3D12 native accessor used on a non-D3D12 device.");
    return *pDevice;
}
} // namespace

NativeDevice GetNative(IDevice& device)
{
    D3D12Device& d3d12Device = AsD3D12(device);
    return NativeDevice{.Device = &d3d12Device.GetNativeDevice(),
                        .GraphicsQueue = &d3d12Device.GetDirectQueue(),
                        .ResourceHeap = &d3d12Device.GetResourceHeap()};
}

ID3D12GraphicsCommandList* GetNative(ICommandList& commandList)
{
    // Called while recording a frame, so a static_cast: the device that allocated the
    // list is the one whose backend recorded everything else into it.
    return static_cast<D3D12CommandList&>(commandList).NativeForRecording();
}

DXGI_FORMAT GetNativeFormat(Format format)
{
    return ToDxgi(format);
}

NativeDescriptor AllocateResourceDescriptor(IDevice& device)
{
    return AsD3D12(device).AllocateResourceDescriptor();
}

void FreeResourceDescriptor(IDevice& device, D3D12_GPU_DESCRIPTOR_HANDLE descriptor)
{
    AsD3D12(device).FreeResourceDescriptor(descriptor);
}
} // namespace Hikari::Rhi::D3D12
