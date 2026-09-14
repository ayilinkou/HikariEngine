#pragma once

#include <directx/d3d12.h>
#include <directx/dxgiformat.h>

#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>

/**
 * The one sanctioned way to get a D3D12 object out of an IDevice, and D3D12's half of
 * the escape hatch VulkanNative.h is for Vulkan.
 *
 * It exists because ImGui's DX12 backend takes a raw device, a queue, a command list
 * and the shader-visible heap its texture descriptors live in, and wrapping ImGui to
 * avoid that is not worth doing. Anything that reaches in here will not compile
 * against another backend, which is the point: the leak is listed, in one file, rather
 * than spread through the renderer.
 */
namespace Hikari::Rhi::D3D12
{
/** Raw objects, for C APIs such as ImGui that take them by value. */
struct NativeDevice
{
    ID3D12Device* Device = nullptr;

    /** The direct queue, which graphics and compute submissions go to. */
    ID3D12CommandQueue* GraphicsQueue = nullptr;

    /**
     * The shader-visible resource heap every command list binds. A descriptor a table
     * points at has to live here, since only one resource heap is bound at a time.
     */
    ID3D12DescriptorHeap* ResourceHeap = nullptr;
};

/** Throws for a device of another backend. */
NativeDevice GetNative(IDevice& device);

/**
 * The list a command list records into.
 *
 * Whatever records into it directly sets state the command list cannot see, so asking
 * for it makes the list forget what it had bound: the next pipeline, bind group and
 * vertex buffers it is given are all set again rather than skipped as unchanged.
 */
ID3D12GraphicsCommandList* GetNative(ICommandList& commandList);

/** The format a view of `format` names. */
DXGI_FORMAT GetNativeFormat(Format format);

/** One descriptor in the resource heap, by the two handles D3D12 addresses it with. */
struct NativeDescriptor
{
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu{};
};

/**
 * A single descriptor from the resource heap, out of the capacity bind groups share.
 * Throws when the heap is full, naming the capacity to raise.
 */
NativeDescriptor AllocateResourceDescriptor(IDevice& device);

/**
 * Returns a descriptor AllocateResourceDescriptor handed out. Nothing may still use it:
 * a list submitted with a table pointing at it reads whatever the descriptor is
 * rewritten to.
 */
void FreeResourceDescriptor(IDevice& device, D3D12_GPU_DESCRIPTOR_HANDLE descriptor);
} // namespace Hikari::Rhi::D3D12
